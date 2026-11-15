/**
 * @file PoolResilienceAndAsyncTest.cpp
 * @brief GamePool 纯 UObject 协程异步分帧预热与内存裁剪/应急泄洪单元测试
 */

#include "Subsystem/ObjectPoolSubsystem.h"
#include "Async/FCancellationTokenSource.h"
#include "Async/UTimeSlicedTaskSubsystem.h"
#include "Misc/CoreDelegates.h"
#include "Tests/GamePoolableTestActor.h"
#include "Tests/GamePoolTestUtils.h"
#include "Tests/UGamePoolableTestObject.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

using Game::Pool::TestUtils::GetTestWorld;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolAsyncUObjectPrewarmTest,
    "Game.Pool.Async.UObjectPrewarmAsync",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证纯 UObject 的 C++20 协程分帧渐进预热与借还功能
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGamePoolAsyncUObjectPrewarmTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Subsystem = World->GetSubsystem<UObjectPoolSubsystem>();
    UTimeSlicedTaskSubsystem* TaskSubsystem = World->GetSubsystem<UTimeSlicedTaskSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Subsystem);
    TestNotNull(TEXT("时间切片子系统有效"), TaskSubsystem);
    if (!Subsystem || !TaskSubsystem) {
        return false;
    }

    Subsystem->ClearAllPools();
    Subsystem->ClearAllObjectPools();
    Subsystem->ResetTelemetry();

    TaskSubsystem->SetFrameBudgetMilliseconds(50.0);

    const TSubclassOf<UObject> TargetClass = UGamePoolableTestObject::StaticClass();
    TGameCoroTask<int32> PrewarmTask = Subsystem->PrewarmObjectPoolAsync(
        TargetClass,
        6,
        2
    );

    int32 SafetyTicks = 0;
    while (!PrewarmTask.IsDone() && SafetyTicks < 30) {
        TaskSubsystem->Tick(0.016f);
        ++SafetyTicks;
    }

    TestTrue(TEXT("协程预热任务正常完成"), PrewarmTask.IsDone());
    TestTrue(TEXT("预热结果状态成功"), PrewarmTask.GetResult().IsSuccess());
    const int32 ResultCount = PrewarmTask.GetResult().GetValue();
    TestEqual(TEXT("实际预热生成6个纯UObject"), ResultCount, 6);

    FPoolTelemetrySnapshot Snapshot = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("池空闲实例为6"), Snapshot.TotalIdleCount, 6);
    TestEqual(TEXT("池活跃实例为0"), Snapshot.TotalActiveCount, 0);

    TGameStatusResult<UGamePoolableTestObject*> Acq1 = Subsystem->AcquireObject<UGamePoolableTestObject>(TargetClass);
    TGameStatusResult<UGamePoolableTestObject*> Acq2 = Subsystem->AcquireObject<UGamePoolableTestObject>(TargetClass);

    TestTrue(TEXT("借出第1个UObject成功"), Acq1.IsSuccess());
    TestTrue(TEXT("借出第2个UObject成功"), Acq2.IsSuccess());

    FPoolTelemetrySnapshot MidSnapshot = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("借出后命中数为2"), MidSnapshot.CacheHits, int64(2));
    TestEqual(TEXT("借出后活跃实例数为2"), MidSnapshot.TotalActiveCount, 2);
    TestEqual(TEXT("借出后剩余空闲数为4"), MidSnapshot.TotalIdleCount, 4);

    if (Acq1.IsSuccess()) {
        Subsystem->ReleaseObject(Acq1.GetValue());
    }
    if (Acq2.IsSuccess()) {
        Subsystem->ReleaseObject(Acq2.GetValue());
    }

    FPoolTelemetrySnapshot FinalSnapshot = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("全部归还后活跃实例恢复为0"), FinalSnapshot.TotalActiveCount, 0);
    TestEqual(TEXT("全部归还后空闲实例恢复为6"), FinalSnapshot.TotalIdleCount, 6);

    Subsystem->ClearAllObjectPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolAsyncUObjectPrewarmCancellationTest,
    "Game.Pool.Async.UObjectPrewarmCancellation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证纯 UObject 协程分帧预热在外部协作令牌取消时的快速熔断
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGamePoolAsyncUObjectPrewarmCancellationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Subsystem = World->GetSubsystem<UObjectPoolSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Subsystem);
    if (!Subsystem) {
        return false;
    }

    Subsystem->ClearAllPools();
    Subsystem->ClearAllObjectPools();
    Subsystem->ResetTelemetry();

    FCancellationTokenSource Cts;
    Cts.Cancel();

    const TSubclassOf<UObject> TargetClass = UGamePoolableTestObject::StaticClass();
    TGameCoroTask<int32> CanceledTask = Subsystem->PrewarmObjectPoolAsync(
        TargetClass,
        10,
        2,
        Cts.GetToken()
    );

    TestTrue(TEXT("已取消任务立即完成"), CanceledTask.IsDone());
    TestTrue(TEXT("取消任务标记为已取消"), CanceledTask.IsCancelled());
    TestFalse(TEXT("取消任务结果状态非成功"), CanceledTask.GetResult().IsSuccess());

    FPoolTelemetrySnapshot Snapshot = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("取消后空闲实例为0"), Snapshot.TotalIdleCount, 0);

    Subsystem->ClearAllObjectPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolResilienceFlushAllTest,
    "Game.Pool.Resilience.FlushAllIdleInstances",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证应急泄洪接口对 Actor 与纯 UObject 空闲实例的彻底清退与保底留存
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGamePoolResilienceFlushAllTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Subsystem = World->GetSubsystem<UObjectPoolSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Subsystem);
    if (!Subsystem) {
        return false;
    }

    Subsystem->ClearAllPools();
    Subsystem->ClearAllObjectPools();
    Subsystem->ResetTelemetry();

    FPoolEvictionSettings ActorSettings;
    ActorSettings.bEnableEviction = true;
    ActorSettings.MinPreservedCount = 1;
    Subsystem->SetActorPoolEvictionSettings(AGamePoolableTestActor::StaticClass(), ActorSettings);

    FPoolEvictionSettings ObjSettings;
    ObjSettings.bEnableEviction = true;
    ObjSettings.MinPreservedCount = 2;
    Subsystem->SetObjectPoolEvictionSettings(UGamePoolableTestObject::StaticClass(), ObjSettings);

    Subsystem->Prewarm(AGamePoolableTestActor::StaticClass(), 4);
    Subsystem->PrewarmObjectPool(UGamePoolableTestObject::StaticClass(), 4);

    FPoolTelemetrySnapshot PrewarmSnap = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("初始空闲实例为8 (4 Actor + 4 Object)"), PrewarmSnap.TotalIdleCount, 8);

    const int32 PreservedFlushCount = Subsystem->FlushAllIdleInstances(true);
    TestEqual(TEXT("保底泄洪销毁5个实例 (3 Actor + 2 Object)"), PreservedFlushCount, 5);

    FPoolTelemetrySnapshot MidSnap = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("保底泄洪后空闲实例保留为3 (1 Actor + 2 Object)"), MidSnap.TotalIdleCount, 3);

    const int32 CompleteFlushCount = Subsystem->FlushAllIdleInstances(false);
    TestEqual(TEXT("彻底泄洪销毁剩余3个实例"), CompleteFlushCount, 3);

    FPoolTelemetrySnapshot FinalSnap = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("彻底泄洪后空闲实例降为0"), FinalSnap.TotalIdleCount, 0);

    Subsystem->ClearAllPools();
    Subsystem->ClearAllObjectPools();
    Subsystem->SetActorPoolEvictionSettings(AGamePoolableTestActor::StaticClass(), FPoolEvictionSettings{});
    Subsystem->SetObjectPoolEvictionSettings(UGamePoolableTestObject::StaticClass(), FPoolEvictionSettings{});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolResilienceMemoryTrimTriggerTest,
    "Game.Pool.Resilience.MemoryTrimTrigger",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证系统内存裁剪委托广播响应与开关管控
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGamePoolResilienceMemoryTrimTriggerTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Subsystem = World->GetSubsystem<UObjectPoolSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Subsystem);
    if (!Subsystem) {
        return false;
    }

    Subsystem->ClearAllPools();
    Subsystem->ClearAllObjectPools();
    Subsystem->ResetTelemetry();
    Subsystem->SetActorPoolEvictionSettings(AGamePoolableTestActor::StaticClass(), FPoolEvictionSettings{});
    Subsystem->SetObjectPoolEvictionSettings(UGamePoolableTestObject::StaticClass(), FPoolEvictionSettings{});

    Subsystem->Prewarm(AGamePoolableTestActor::StaticClass(), 3);
    Subsystem->PrewarmObjectPool(UGamePoolableTestObject::StaticClass(), 3);

    FPoolTelemetrySnapshot PreSnap = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("预热后空闲实例数为6"), PreSnap.TotalIdleCount, 6);

    Subsystem->SetLowMemoryAutoFlushEnabled(false);
    TestFalse(TEXT("低内存自动泄洪已关闭"), Subsystem->IsLowMemoryAutoFlushEnabled());

    FCoreDelegates::GetMemoryTrimDelegate().Broadcast();

    FPoolTelemetrySnapshot DisabledSnap = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("开关关闭时收到内存裁剪广播不予泄洪"), DisabledSnap.TotalIdleCount, 6);

    Subsystem->SetLowMemoryAutoFlushEnabled(true);
    TestTrue(TEXT("低内存自动泄洪已开启"), Subsystem->IsLowMemoryAutoFlushEnabled());

    FCoreDelegates::GetMemoryTrimDelegate().Broadcast();

    FPoolTelemetrySnapshot TrimmedSnap = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("开关开启时收到内存裁剪广播成功清退所有无保底空闲实例"), TrimmedSnap.TotalIdleCount, 0);

    FPoolEvictionSettings ActorSettings;
    ActorSettings.bEnableEviction = true;
    ActorSettings.MinPreservedCount = 1;
    Subsystem->SetActorPoolEvictionSettings(AGamePoolableTestActor::StaticClass(), ActorSettings);

    FPoolEvictionSettings ObjSettings;
    ObjSettings.bEnableEviction = true;
    ObjSettings.MinPreservedCount = 1;
    Subsystem->SetObjectPoolEvictionSettings(UGamePoolableTestObject::StaticClass(), ObjSettings);

    Subsystem->Prewarm(AGamePoolableTestActor::StaticClass(), 2);
    Subsystem->PrewarmObjectPool(UGamePoolableTestObject::StaticClass(), 2);

    FCoreDelegates::GetMemoryTrimDelegate().Broadcast();

    FPoolTelemetrySnapshot PreservedTrimSnap = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("内存裁剪广播应尊重保底配置保留2个实例"), PreservedTrimSnap.TotalIdleCount, 2);

    Subsystem->ClearAllPools();
    Subsystem->ClearAllObjectPools();
    Subsystem->SetActorPoolEvictionSettings(AGamePoolableTestActor::StaticClass(), FPoolEvictionSettings{});
    Subsystem->SetObjectPoolEvictionSettings(UGamePoolableTestObject::StaticClass(), FPoolEvictionSettings{});
    Subsystem->SetLowMemoryAutoFlushEnabled(true);
    return true;
}

#endif
