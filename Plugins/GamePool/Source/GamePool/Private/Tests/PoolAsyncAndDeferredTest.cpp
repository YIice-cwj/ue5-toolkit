/**
 * @file PoolAsyncAndDeferredTest.cpp
 * @brief 协程流式异步预热（含中途取消与软类引用）与多线程并发无锁延迟归还自动化测试
 */

#include "Subsystem/UObjectPoolSubsystem.h"

#include "Async/FCancellationTokenSource.h"
#include "Async/ParallelFor.h"
#include "Async/UTimeSlicedTaskSubsystem.h"
#include "Tests/AGamePoolableTestActor.h"
#include "Tests/UGamePoolableTestObject.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Tests/GamePoolTestUtils.h"

using Game::Pool::TestUtils::GetTestWorld;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolPrewarmAsyncTest,
    "Game.Pool.PrewarmAsync",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Actor C++20 协程分帧预热、容量上限截断、初始取消与中途取消熔断
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
    Pool->SetMaxPoolCapacity(ActorClass, 100);

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
    TestEqual(TEXT("分帧预热完成全部 5 个空闲实例"), Pool->GetPoolStats(ActorClass).IdleCount, 5);

    Pool->SetMaxPoolCapacity(ActorClass, 7);
    TGameCoroTask<int32> CapTask = Pool->PrewarmAsync(ActorClass, 20, 2);
    SafetyTicks = 0;
    while (!CapTask.IsDone() && SafetyTicks < 20) {
        TaskSubsystem->Tick(0.016f);
        ++SafetyTicks;
    }
    TestTrue(TEXT("触达容量上限时协程正常完成"), CapTask.IsDone());
    TestEqual(TEXT("分帧预热触达最大空闲容量上限时自动短路停机"), Pool->GetPoolStats(ActorClass).IdleCount, 7);
    Pool->SetMaxPoolCapacity(ActorClass, 100);

    FCancellationTokenSource PreCanceledCts;
    PreCanceledCts.Cancel();
    TGameCoroTask<int32> CanceledTask = Pool->PrewarmAsync(ActorClass, 4, 2, PreCanceledCts.GetToken());
    TestTrue(TEXT("已取消令牌预热协程立即完成"), CanceledTask.IsDone());
    TestTrue(TEXT("已取消令牌任务状态为 Cancelled"), CanceledTask.IsCancelled());
    TestEqual(TEXT("取消分帧预热后不再新增实例"), Pool->GetPoolStats(ActorClass).IdleCount, 7);

    Pool->ClearPool(ActorClass);
    FCancellationTokenSource MidCts;
    TGameCoroTask<int32> MidCancelTask = Pool->PrewarmAsync(ActorClass, 10, 2, MidCts.GetToken());
    TestFalse(TEXT("首步让出后任务尚在运行中"), MidCancelTask.IsDone());
    TestEqual(TEXT("首步已生成 2 个实例"), Pool->GetPoolStats(ActorClass).IdleCount, 2);
    MidCts.Cancel();
    TaskSubsystem->Tick(0.016f);
    TestTrue(TEXT("中途取消后任务立即中止完成"), MidCancelTask.IsDone());
    TestTrue(TEXT("中途取消任务终态为 Cancelled"), MidCancelTask.IsCancelled());
    TestEqual(TEXT("中途取消后不再继续生成剩余实例"), Pool->GetPoolStats(ActorClass).IdleCount, 2);

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
 * @brief 验证 Actor 与 UObject 基于软对象路径及 TSoftClassPtr 的流式协程预热与错配路径防护
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
    const TSubclassOf<UObject> ObjectClass = UGamePoolableTestObject::StaticClass();
    Pool->ClearAllPools();

    const FSoftObjectPath ValidActorPath(ActorClass);
    TGameCoroTask<int32> PrewarmTask = Pool->PrewarmAsync(ValidActorPath, 4, 1);
    int32 SafetyTicks = 0;
    while (!PrewarmTask.IsDone() && SafetyTicks < 20) {
        TaskSubsystem->Tick(0.016f);
        ++SafetyTicks;
    }
    TestTrue(TEXT("Actor 软路径流式预热协程执行完成"), PrewarmTask.IsDone());
    TestTrue(TEXT("Actor 软路径流式预热结果成功"), PrewarmTask.GetResult().IsSuccess());
    TestEqual(TEXT("Actor 软路径流式预热成功生成 4 个实例"), PrewarmTask.GetResult().GetValue(), 4);
    TestEqual(TEXT("Actor 池内实际空闲数为 4"), Pool->GetPoolStats(ActorClass).IdleCount, 4);

    const TSoftClassPtr<AActor> SoftActorClass(ActorClass);
    TGameCoroTask<int32> SoftActorTask = Pool->PrewarmAsync(SoftActorClass, 2, 2);
    SafetyTicks = 0;
    while (!SoftActorTask.IsDone() && SafetyTicks < 20) {
        TaskSubsystem->Tick(0.016f);
        ++SafetyTicks;
    }
    TestTrue(TEXT("TSoftClassPtr<AActor> 重载预热完成"), SoftActorTask.IsDone());
    TestEqual(TEXT("累计 Actor 空闲数为 6"), Pool->GetPoolStats(ActorClass).IdleCount, 6);

    const TSoftClassPtr<UObject> SoftObjectClass(ObjectClass);
    TGameCoroTask<int32> SoftObjTask = Pool->PrewarmObjectPoolAsync(SoftObjectClass, 5, 2);
    SafetyTicks = 0;
    while (!SoftObjTask.IsDone() && SafetyTicks < 20) {
        TaskSubsystem->Tick(0.016f);
        ++SafetyTicks;
    }
    TestTrue(TEXT("UObject 软类引用流式预热完成"), SoftObjTask.IsDone());
    TestTrue(TEXT("UObject 软类引用流式预热成功"), SoftObjTask.GetResult().IsSuccess());
    TestEqual(TEXT("UObject 软类引用成功生成 5 个实例"), SoftObjTask.GetResult().GetValue(), 5);
    TestEqual(TEXT("UObject 池内空闲数为 5"), Pool->GetObjectPoolStats(ObjectClass).IdleCount, 5);

    TGameCoroTask<int32> WrongClassPathTask = Pool->PrewarmObjectPoolAsync(ValidActorPath, 3, 2);
    TestTrue(TEXT("向 UObject 路径预热传入 AActor 路径安全返回 0"), WrongClassPathTask.IsDone() && WrongClassPathTask.GetResult().GetValue() == 0);

    FCancellationTokenSource Cts;
    Cts.Cancel();
    TGameCoroTask<int32> CanceledTask = Pool->PrewarmAsync(ValidActorPath, 5, 1, Cts.GetToken());
    TestTrue(TEXT("已取消令牌的预热协程立即完成"), CanceledTask.IsDone());
    TestTrue(TEXT("已取消令牌的预热协程返回失败状态"), CanceledTask.GetResult().IsFailure());

    Pool->ClearAllPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolDeferredReleaseTest,
    "Game.Pool.DeferredRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多线程并发无锁延迟归还、精确成功计数、重复/非法项过滤与分批收割限流
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
    const TSubclassOf<UObject> ObjClass = UGamePoolableTestObject::StaticClass();
    Pool->ClearAllPools();
    Pool->SetMaxPoolCapacity(ActorClass, 100);
    Pool->SetMaxObjectPoolCapacity(ObjClass, 100);

    AGamePoolableTestActor* Actor = Pool->AcquireActor<AGamePoolableTestActor>(ActorClass).GetValue();
    TestNotNull(TEXT("借出 Actor 有效"), Actor);
    if (!Actor) {
        return false;
    }

    const TGameStatusResult<void> RejectActorAsObj = Pool->ReleaseObjectDeferred(Actor);
    TestTrue(TEXT("ReleaseObjectDeferred 拒绝 AActor 实例"), RejectActorAsObj.IsFailure());
    TestEqual(TEXT("拒绝错误码为 InvalidClass"), RejectActorAsObj.GetError().ErrorCode, Game::PoolErrorCodes::InvalidClass);

    TestTrue(TEXT("首次无锁延迟入队 Actor 成功"), Pool->ReleaseActorDeferred(Actor).IsSuccess());
    TestTrue(TEXT("重复入队同一 Actor 入队本身成功"), Pool->ReleaseActorDeferred(Actor).IsSuccess());
    TestEqual(TEXT("收割前积压计数为 2"), Pool->GetPendingDeferredReleaseCount(), 2);

    const int32 HarvestedSingle = Pool->ProcessDeferredReleases(64);
    TestEqual(TEXT("重复入队仅成功归还 1 次，不虚报成功数"), HarvestedSingle, 1);
    TestEqual(TEXT("收割后积压清零"), Pool->GetPendingDeferredReleaseCount(), 0);
    TestEqual(TEXT("收割后活跃数归零"), Pool->GetPoolStats(ActorClass).ActiveCount, 0);
    TestEqual(TEXT("收割后空闲数为 1"), Pool->GetPoolStats(ActorClass).IdleCount, 1);
    TestTrue(TEXT("收割后 Actor 已被休眠隐藏"), Actor->IsHidden());

    constexpr int32 ConcurrentObjectCount = 32;
    TArray<UGamePoolableTestObject*> ConcurrentObjects;
    ConcurrentObjects.Reserve(ConcurrentObjectCount);
    for (int32 Index = 0; Index < ConcurrentObjectCount; ++Index) {
        UGamePoolableTestObject* Obj = Pool->AcquireObject<UGamePoolableTestObject>(ObjClass).GetValue();
        ConcurrentObjects.Add(Obj);
    }
    TestEqual(TEXT("并发测试前活跃 UObject 为 32"), Pool->GetObjectPoolStats(ObjClass).ActiveCount, ConcurrentObjectCount);

    std::atomic<int32> EnqueueSuccessCount{0};
    ParallelFor(ConcurrentObjectCount, [Pool, &ConcurrentObjects, &EnqueueSuccessCount](const int32 Index) {
        if (Pool->ReleaseObjectDeferred(ConcurrentObjects[Index]).IsSuccess()) {
            EnqueueSuccessCount.fetch_add(1, std::memory_order_relaxed);
        }
    });
    TestEqual(TEXT("多线程并发无锁入队 32 个 UObject 全部成功"), EnqueueSuccessCount.load(), ConcurrentObjectCount);

    TestEqual(TEXT("分批限流第 1 步最多收割 10 个"), Pool->ProcessDeferredReleases(10), 10);
    TestEqual(TEXT("分批限流第 2 步最多收割 10 个"), Pool->ProcessDeferredReleases(10), 10);
    TestEqual(TEXT("分批限流第 3 步收割剩余 12 个"), Pool->ProcessDeferredReleases(64), 12);
    TestEqual(TEXT("全部收割后 UObject 活跃数归零"), Pool->GetObjectPoolStats(ObjClass).ActiveCount, 0);
    TestEqual(TEXT("全部收割后 UObject 空闲数为 32"), Pool->GetObjectPoolStats(ObjClass).IdleCount, ConcurrentObjectCount);

    UGamePoolableTestObject* OverflowObj = Pool->AcquireObject<UGamePoolableTestObject>(ObjClass).GetValue();
    TestNotNull(TEXT("满队列溢出测试对象有效"), OverflowObj);
    int32 EnqueuedUntilFull = 0;
    TGameStatusResult<void> OverflowRes = TGameStatusResult<void>::Success();
    for (int32 Index = 0; Index < 1100; ++Index) {
        OverflowRes = Pool->ReleaseObjectDeferred(OverflowObj);
        if (OverflowRes.IsFailure()) {
            break;
        }
        ++EnqueuedUntilFull;
    }
    TestEqual(TEXT("无锁环形缓冲严格在 1024 容量处触发满队拒收"), EnqueuedUntilFull, 1024);
    TestTrue(TEXT("超出容量入队返回 Failure"), OverflowRes.IsFailure());
    TestEqual(TEXT("满队错误归属 ModuleId::Pool"), OverflowRes.GetError().ModuleId, Game::ModuleId::Pool);
    TestEqual(TEXT("满队错误码为 DeferredQueueFull"), OverflowRes.GetError().ErrorCode, Game::PoolErrorCodes::DeferredQueueFull);
    TestEqual(TEXT("清排满队列仅成功归还 1 次并清空 1024 条积压"), Pool->ProcessDeferredReleases(2048), 1);
    TestEqual(TEXT("清排后延迟队列积压归零"), Pool->GetPendingDeferredReleaseCount(), 0);

    Pool->ClearAllPools();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
