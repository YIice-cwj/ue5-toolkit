// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectPool/ObjectPoolSubsystem.h"

#include "Async/FCancellationTokenSource.h"
#include "Async/UTimeSlicedTaskSubsystem.h"
#include "Tests/GamePoolableTestActor.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"

namespace {

/**
 * @brief 获取测试世界指针
 * @return 当前有效的 UWorld 指针，若不存在则返回 nullptr
 */
UWorld* GetTestWorld() {
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World()) {
            return Context.World();
        }
    }
    return nullptr;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolPrewarmAsyncTest,
    "Game.Pool.PrewarmAsync",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证基于 Actor 类型的 C++20 协程流式分帧预热、容量上限截断与协同取消令牌
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameObjectPoolPrewarmAsyncTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Pool = World->GetSubsystem<UObjectPoolSubsystem>();
    UTimeSlicedTaskSubsystem* TaskSubsystem = World->GetSubsystem<UTimeSlicedTaskSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Pool);
    TestNotNull(TEXT("时间切片子系统有效"), TaskSubsystem);
    if (!Pool || !TaskSubsystem) {
        return false;
    }

    const TSubclassOf<AActor> ActorClass = AGamePoolableTestActor::StaticClass();
    Pool->ClearPool(ActorClass);

    TaskSubsystem->SetFrameBudgetMilliseconds(50.0);
    TGameCoroTask<int32> InvalidAsyncPrewarm = Pool->PrewarmAsync(TSubclassOf<AActor>(nullptr), 5, 2);
    TestTrue(TEXT("空类协程预热立即完成"), InvalidAsyncPrewarm.IsDone());
    TestTrue(TEXT("空类协程结果状态正常"), InvalidAsyncPrewarm.GetResult().IsSuccess());
    TestEqual(TEXT("空类协程预热返回 0"), InvalidAsyncPrewarm.GetResult().GetValue(), 0);

    TGameCoroTask<int32> PrewarmTask = Pool->PrewarmAsync(ActorClass, 5, 2);
    int32 SafetyTicks = 0;
    while (!PrewarmTask.IsDone() && SafetyTicks < 20) {
        TaskSubsystem->Tick(0.016f);
        ++SafetyTicks;
    }
    TestTrue(TEXT("分帧渐进预热完成"), PrewarmTask.IsDone());
    TestTrue(TEXT("预热结果成功"), PrewarmTask.GetResult().IsSuccess());
    TestEqual(TEXT("生成数量正确"), PrewarmTask.GetResult().GetValue(), 5);
    const FPoolStats PrewarmStats = Pool->GetPoolStats(ActorClass);
    TestEqual(TEXT("分帧预热完成全部 5 个空闲实例"), PrewarmStats.IdleCount, 5);

    Pool->SetMaxPoolCapacity(ActorClass, 7);
    TGameCoroTask<int32> CapTask = Pool->PrewarmAsync(ActorClass, 20, 2);
    SafetyTicks = 0;
    while (!CapTask.IsDone() && SafetyTicks < 20) {
        TaskSubsystem->Tick(0.016f);
        ++SafetyTicks;
    }
    TestTrue(TEXT("触达容量上限时协程正常完成"), CapTask.IsDone());
    const FPoolStats CapStats = Pool->GetPoolStats(ActorClass);
    TestEqual(TEXT("分帧预热触达最大空闲容量上限时自动短路停机"), CapStats.IdleCount, 7);
    Pool->SetMaxPoolCapacity(ActorClass, 100);

    FCancellationTokenSource Cts;
    Cts.Cancel();
    TGameCoroTask<int32> CanceledTask = Pool->PrewarmAsync(ActorClass, 4, 2, Cts.GetToken());
    TestTrue(TEXT("已取消令牌预热协程立即完成"), CanceledTask.IsDone());
    const FPoolStats CanceledStats = Pool->GetPoolStats(ActorClass);
    TestEqual(TEXT("取消分帧预热后不再新增实例"), CanceledStats.IdleCount, 7);

    Pool->ClearPool(ActorClass);
    TGameCoroTask<int32> FullAsyncPrewarmTask = Pool->PrewarmAsync(ActorClass, 6, 2);
    SafetyTicks = 0;
    while (!FullAsyncPrewarmTask.IsDone() && SafetyTicks < 20) {
        TaskSubsystem->Tick(0.016f);
        ++SafetyTicks;
    }
    TestTrue(TEXT("分帧渐进预热协程在多帧步进后完成"), FullAsyncPrewarmTask.IsDone());
    TestTrue(TEXT("分帧渐进预热协程结果状态成功"), FullAsyncPrewarmTask.GetResult().IsSuccess());
    TestEqual(TEXT("分帧渐进预热协程返回成功生成实例数 6"), FullAsyncPrewarmTask.GetResult().GetValue(), 6);
    const FPoolStats FullStats = Pool->GetPoolStats(ActorClass);
    TestEqual(TEXT("对象池内实际包含 6 个空闲实例"), FullStats.IdleCount, 6);

    TaskSubsystem->SetFrameBudgetMilliseconds(FTimeSliceScheduler::DEFAULT_FRAME_BUDGET_MS);
    Pool->ClearPool(ActorClass);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolPrewarmPathAsyncTest,
    "Game.Pool.PrewarmPathAsync",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证基于软引用的 C++20 协程流式预热、时间分片让出与协同取消熔断
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameObjectPoolPrewarmPathAsyncTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Pool = World->GetSubsystem<UObjectPoolSubsystem>();
    UTimeSlicedTaskSubsystem* TaskSubsystem = World->GetSubsystem<UTimeSlicedTaskSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Pool);
    TestNotNull(TEXT("时间切片子系统有效"), TaskSubsystem);
    if (!Pool || !TaskSubsystem) {
        return false;
    }

    const TSubclassOf<AActor> ActorClass = AGamePoolableTestActor::StaticClass();
    Pool->ClearPool(ActorClass);

    const FSoftObjectPath ValidPath(ActorClass);
    TGameCoroTask<int32> PrewarmTask = Pool->PrewarmAsync(ValidPath, 4, 1);

    int32 SafetyTicks = 0;
    while (!PrewarmTask.IsDone() && SafetyTicks < 20) {
        TaskSubsystem->Tick(0.016f);
        ++SafetyTicks;
    }

    TestTrue(TEXT("软引用流式预热协程执行完成"), PrewarmTask.IsDone());
    TestTrue(TEXT("软引用流式预热结果状态成功"), PrewarmTask.GetResult().IsSuccess());
    TestEqual(TEXT("软引用流式预热成功生成 4 个实例"), PrewarmTask.GetResult().GetValue(), 4);

    const FPoolStats PathStats = Pool->GetPoolStats(ActorClass);
    TestEqual(TEXT("池内实际空闲数为 4"), PathStats.IdleCount, 4);

    FCancellationTokenSource Cts;
    Cts.Cancel();
    TGameCoroTask<int32> CanceledTask = Pool->PrewarmAsync(ValidPath, 5, 1, Cts.GetToken());
    TestTrue(TEXT("已取消令牌的预热协程立即完成"), CanceledTask.IsDone());
    TestTrue(TEXT("已取消令牌的预热协程返回失败状态"), CanceledTask.GetResult().IsFailure());

    Pool->ClearPool(ActorClass);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolDeferredReleaseTest,
    "Game.Pool.DeferredRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多线程/工作线程安全延迟归还与主线程批量收割
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameObjectPoolDeferredReleaseTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Pool = World->GetSubsystem<UObjectPoolSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Pool);
    if (!Pool) {
        return false;
    }

    const TSubclassOf<AActor> ActorClass = AGamePoolableTestActor::StaticClass();
    Pool->ClearPool(ActorClass);

    const TGameStatusResult<AGamePoolableTestActor*> ActorRes = Pool->AcquireActor<AGamePoolableTestActor>(ActorClass);
    TestTrue(TEXT("借出 Actor 成功"), ActorRes.IsSuccess());
    AGamePoolableTestActor* Actor = ActorRes.GetValue();
    TestNotNull(TEXT("Actor 有效"), Actor);
    if (!Actor) {
        return false;
    }

    TestTrue(TEXT("无锁延迟入队归还成功"), Pool->ReleaseActorDeferred(Actor));

    const FPoolStats PreHarvestStats = Pool->GetPoolStats(ActorClass);
    TestEqual(TEXT("收割前仍处于活跃状态"), PreHarvestStats.ActiveCount, 1);
    TestEqual(TEXT("收割前空闲数为 0"), PreHarvestStats.IdleCount, 0);

    const int32 HarvestedCount = Pool->ProcessDeferredReleases(64);
    TestEqual(TEXT("主线程成功收割 1 个延迟归还实例"), HarvestedCount, 1);

    const FPoolStats PostHarvestStats = Pool->GetPoolStats(ActorClass);
    TestEqual(TEXT("收割后活跃数归零"), PostHarvestStats.ActiveCount, 0);
    TestEqual(TEXT("收割后空闲数为 1"), PostHarvestStats.IdleCount, 1);
    TestTrue(TEXT("收割后 Actor 已被休眠隐藏"), Actor->IsHidden());

    Pool->ClearPool(ActorClass);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
