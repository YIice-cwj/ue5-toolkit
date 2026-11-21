/**
 * @file PoolTelemetryTest.cpp
 * @brief GamePool 运行期可观测性与遥测监控 (stat GamePool & Telemetry) 单元测试
 */

#include "Subsystem/UObjectPoolSubsystem.h"
#include "Tests/AGamePoolableTestActor.h"
#include "Tests/UGamePoolableTestObject.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/GamePoolTestUtils.h"

using Game::Pool::TestUtils::GetTestWorld;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolTelemetryHitMissTest,
    "Game.Pool.Telemetry.HitAndMissCalculation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证对象池命中与穿透计数、命中率计算以及失败调用不污染遥测
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGamePoolTelemetryHitMissTest::RunTest([[maybe_unused]] const FString& Parameters) {
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
    Subsystem->ResetTelemetry();

    FPoolTelemetrySnapshot InitialSnapshot = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("初始总借出为0"), InitialSnapshot.TotalAcquires, int64(0));
    TestEqual(TEXT("初始命中为0"), InitialSnapshot.CacheHits, int64(0));
    TestEqual(TEXT("初始穿透为0"), InitialSnapshot.CacheMisses, int64(0));
    TestEqual(TEXT("初始命中率为0"), InitialSnapshot.GetHitRatePercentage(), 0.0f);

    TestTrue(TEXT("非法空类借出失败"), Subsystem->AcquireActor<AActor>(nullptr).IsFailure());
    TestEqual(TEXT("失败借出不污染 TotalAcquires"), Subsystem->GetTelemetrySnapshot().TotalAcquires, int64(0));

    Subsystem->Prewarm(AGamePoolableTestActor::StaticClass(), 2);
    FPoolTelemetrySnapshot PrewarmedSnapshot = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("预热后空闲Actor为2"), PrewarmedSnapshot.TotalIdleCount, 2);
    TestEqual(TEXT("预热后借出为0"), PrewarmedSnapshot.TotalAcquires, int64(0));

    TGameStatusResult<AGamePoolableTestActor*> Acq1 =
        Subsystem->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass());
    TestTrue(TEXT("第1次借出成功"), Acq1.IsSuccess());

    FPoolTelemetrySnapshot Snapshot1 = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("第1次借出后总借出为1"), Snapshot1.TotalAcquires, int64(1));
    TestEqual(TEXT("第1次借出命中为1"), Snapshot1.CacheHits, int64(1));
    TestEqual(TEXT("第1次借出穿透为0"), Snapshot1.CacheMisses, int64(0));
    TestEqual(TEXT("第1次命中率为100%"), Snapshot1.GetHitRatePercentage(), 100.0f);

    TGameStatusResult<AGamePoolableTestActor*> Acq2 =
        Subsystem->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass());
    TestTrue(TEXT("第2次借出成功"), Acq2.IsSuccess());

    FPoolTelemetrySnapshot Snapshot2 = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("第2次借出后总借出为2"), Snapshot2.TotalAcquires, int64(2));
    TestEqual(TEXT("第2次借出命中为2"), Snapshot2.CacheHits, int64(2));
    TestEqual(TEXT("第2次借出穿透为0"), Snapshot2.CacheMisses, int64(0));
    TestEqual(TEXT("第2次命中率为100%"), Snapshot2.GetHitRatePercentage(), 100.0f);

    TGameStatusResult<AGamePoolableTestActor*> Acq3 =
        Subsystem->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass());
    TestTrue(TEXT("第3次借出成功（池空新生成）"), Acq3.IsSuccess());

    FPoolTelemetrySnapshot Snapshot3 = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("第3次借出后总借出为3"), Snapshot3.TotalAcquires, int64(3));
    TestEqual(TEXT("第3次借出命中为2"), Snapshot3.CacheHits, int64(2));
    TestEqual(TEXT("第3次借出穿透为1"), Snapshot3.CacheMisses, int64(1));
    const float ExpectedHitRate = (2.0f / 3.0f) * 100.0f;
    TestNearlyEqual(TEXT("3次借出命中率为66.67%"), Snapshot3.GetHitRatePercentage(), ExpectedHitRate, 0.01f);

    if (Acq1.IsSuccess()) {
        (void)Subsystem->ReleaseActor(Acq1.GetValue());
    }
    if (Acq2.IsSuccess()) {
        (void)Subsystem->ReleaseActor(Acq2.GetValue());
    }
    if (Acq3.IsSuccess()) {
        (void)Subsystem->ReleaseActor(Acq3.GetValue());
    }

    Subsystem->ClearAllPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolTelemetryActiveIdleCounterSyncTest,
    "Game.Pool.Telemetry.ActiveIdleCounterSync",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证对象池活跃与空闲实例计数在借出与归还全生命周期中的准确同步
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGamePoolTelemetryActiveIdleCounterSyncTest::RunTest([[maybe_unused]] const FString& Parameters) {
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
    Subsystem->ResetTelemetry();

    Subsystem->Prewarm(AGamePoolableTestActor::StaticClass(), 3);
    Subsystem->PrewarmObjectPool(UGamePoolableTestObject::StaticClass(), 2);

    FPoolTelemetrySnapshot InitialSnapshot = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("初始总空闲实例为5 (3 Actor + 2 Object)"), InitialSnapshot.TotalIdleCount, 5);
    TestEqual(TEXT("初始总活跃实例为0"), InitialSnapshot.TotalActiveCount, 0);

    TGameStatusResult<AGamePoolableTestActor*> ActorRes =
        Subsystem->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass());
    TGameStatusResult<UGamePoolableTestObject*> ObjRes =
        Subsystem->AcquireObject<UGamePoolableTestObject>(UGamePoolableTestObject::StaticClass());

    TestTrue(TEXT("Actor借出成功"), ActorRes.IsSuccess());
    TestTrue(TEXT("UObject借出成功"), ObjRes.IsSuccess());

    FPoolTelemetrySnapshot MidSnapshot = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("借出后总活跃实例为2"), MidSnapshot.TotalActiveCount, 2);
    TestEqual(TEXT("借出后总空闲实例为3"), MidSnapshot.TotalIdleCount, 3);
    TestEqual(TEXT("借出后总借出次数为2"), MidSnapshot.TotalAcquires, int64(2));
    TestEqual(TEXT("借出后全命中为2"), MidSnapshot.CacheHits, int64(2));

    if (ActorRes.IsSuccess()) {
        (void)Subsystem->ReleaseActor(ActorRes.GetValue());
    }
    if (ObjRes.IsSuccess()) {
        (void)Subsystem->ReleaseObject(ObjRes.GetValue());
    }

    FPoolTelemetrySnapshot FinalSnapshot = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("归还后总活跃实例恢复为0"), FinalSnapshot.TotalActiveCount, 0);
    TestEqual(TEXT("归还后总空闲实例恢复为5"), FinalSnapshot.TotalIdleCount, 5);

    Subsystem->ClearAllPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolTelemetryResetTest,
    "Game.Pool.Telemetry.Reset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证重置遥测统计计数器逻辑
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGamePoolTelemetryResetTest::RunTest([[maybe_unused]] const FString& Parameters) {
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
    Subsystem->ResetTelemetry();

    TGameStatusResult<UGamePoolableTestObject*> ObjRes =
        Subsystem->AcquireObject<UGamePoolableTestObject>(UGamePoolableTestObject::StaticClass());
    TestTrue(TEXT("借出UObject成功"), ObjRes.IsSuccess());

    FPoolTelemetrySnapshot BeforeReset = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("重置前总借出为1"), BeforeReset.TotalAcquires, int64(1));
    TestEqual(TEXT("重置前穿透为1"), BeforeReset.CacheMisses, int64(1));

    Subsystem->ResetTelemetry();

    FPoolTelemetrySnapshot AfterReset = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("重置后总借出归0"), AfterReset.TotalAcquires, int64(0));
    TestEqual(TEXT("重置后命中归0"), AfterReset.CacheHits, int64(0));
    TestEqual(TEXT("重置后穿透归0"), AfterReset.CacheMisses, int64(0));
    TestEqual(TEXT("重置后命中率归0"), AfterReset.GetHitRatePercentage(), 0.0f);
    TestEqual(TEXT("活跃实例维持1"), AfterReset.TotalActiveCount, 1);

    if (ObjRes.IsSuccess()) {
        (void)Subsystem->ReleaseObject(ObjRes.GetValue());
    }

    Subsystem->ClearAllPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolTelemetryEvictionTrackingTest,
    "Game.Pool.Telemetry.EvictionTracking",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 TTL 淘汰触发时遥测统计指标的准确同步
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGamePoolTelemetryEvictionTrackingTest::RunTest([[maybe_unused]] const FString& Parameters) {
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
    Subsystem->ResetTelemetry();

    FPoolEvictionSettings Settings;
    Settings.bEnableEviction = true;
    Settings.IdleTimeToLiveSeconds = 10.0f;
    Settings.MinPreservedCount = 1;
    Settings.MaxEvictionsPerStep = 8;
    Subsystem->SetActorPoolEvictionSettings(AGamePoolableTestActor::StaticClass(), Settings);

    Subsystem->Prewarm(AGamePoolableTestActor::StaticClass(), 4);
    FPoolTelemetrySnapshot PrewarmSnapshot = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("预热后空闲数为4"), PrewarmSnapshot.TotalIdleCount, 4);
    TestEqual(TEXT("预热后活跃数为0"), PrewarmSnapshot.TotalActiveCount, 0);

    const double SimulatedNow = FPlatformTime::Seconds() + 20.0;
    const int32 EvictedCount = Subsystem->ForceEvictExpired(8, SimulatedNow);
    TestEqual(TEXT("成功淘汰3个超时Actor（保底留存1个）"), EvictedCount, 3);

    FPoolTelemetrySnapshot AfterEviction = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("淘汰后空闲数降为1"), AfterEviction.TotalIdleCount, 1);
    TestEqual(TEXT("淘汰后活跃数仍为0"), AfterEviction.TotalActiveCount, 0);

    TGameStatusResult<AGamePoolableTestActor*> HitRes =
        Subsystem->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass());
    TestTrue(TEXT("借出保留实例成功"), HitRes.IsSuccess());

    FPoolTelemetrySnapshot HitSnapshot = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("借出保留实例为命中"), HitSnapshot.CacheHits, int64(1));
    TestEqual(TEXT("借出保留实例穿透为0"), HitSnapshot.CacheMisses, int64(0));
    TestEqual(TEXT("池空闲降为0"), HitSnapshot.TotalIdleCount, 0);
    TestEqual(TEXT("池活跃升为1"), HitSnapshot.TotalActiveCount, 1);

    TGameStatusResult<AGamePoolableTestActor*> MissRes =
        Subsystem->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass());
    TestTrue(TEXT("再次借出新生成成功"), MissRes.IsSuccess());

    FPoolTelemetrySnapshot MissSnapshot = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("二次借出命中仍为1"), MissSnapshot.CacheHits, int64(1));
    TestEqual(TEXT("二次借出穿透升为1"), MissSnapshot.CacheMisses, int64(1));
    TestEqual(TEXT("总活跃升为2"), MissSnapshot.TotalActiveCount, 2);
    TestNearlyEqual(TEXT("命中率50%"), MissSnapshot.GetHitRatePercentage(), 50.0f, 0.01f);

    if (HitRes.IsSuccess()) {
        (void)Subsystem->ReleaseActor(HitRes.GetValue());
    }
    if (MissRes.IsSuccess()) {
        (void)Subsystem->ReleaseActor(MissRes.GetValue());
    }

    Subsystem->SetActorPoolEvictionSettings(AGamePoolableTestActor::StaticClass(), FPoolEvictionSettings{});
    Subsystem->ClearAllPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolTelemetryHighThroughputBatchMathTest,
    "Game.Pool.Telemetry.HighThroughputBatchMath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证高通量批量借还交替场景下的遥测代数守恒定律
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGamePoolTelemetryHighThroughputBatchMathTest::RunTest([[maybe_unused]] const FString& Parameters) {
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
    Subsystem->ResetTelemetry();

    constexpr int32 BatchA = 20;
    constexpr int32 ReleaseA = 12;
    constexpr int32 BatchB = 15;

    TArray<AGamePoolableTestActor*> ActiveActors;
    ActiveActors.Reserve(BatchA + BatchB);

    for (int32 Index = 0; Index < BatchA; ++Index) {
        TGameStatusResult<AGamePoolableTestActor*> Res =
            Subsystem->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass());
        TestTrue(TEXT("批次A借出成功"), Res.IsSuccess());
        if (Res.IsSuccess()) {
            ActiveActors.Add(Res.GetValue());
        }
    }

    FPoolTelemetrySnapshot SnapA = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("批次A总借出为20"), SnapA.TotalAcquires, int64(BatchA));
    TestEqual(TEXT("批次A全穿透"), SnapA.CacheMisses, int64(BatchA));
    TestEqual(TEXT("批次A全命中为0"), SnapA.CacheHits, int64(0));
    TestEqual(TEXT("批次A活跃实例为20"), SnapA.TotalActiveCount, BatchA);
    TestEqual(TEXT("批次A空闲实例为0"), SnapA.TotalIdleCount, 0);

    for (int32 Index = 0; Index < ReleaseA; ++Index) {
        (void)Subsystem->ReleaseActor(ActiveActors[Index]);
    }
    ActiveActors.RemoveAt(0, ReleaseA);

    FPoolTelemetrySnapshot SnapRel = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("部分归还后活跃实例为8"), SnapRel.TotalActiveCount, BatchA - ReleaseA);
    TestEqual(TEXT("部分归还后空闲实例为12"), SnapRel.TotalIdleCount, ReleaseA);

    for (int32 Index = 0; Index < BatchB; ++Index) {
        TGameStatusResult<AGamePoolableTestActor*> Res =
            Subsystem->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass());
        TestTrue(TEXT("批次B借出成功"), Res.IsSuccess());
        if (Res.IsSuccess()) {
            ActiveActors.Add(Res.GetValue());
        }
    }

    FPoolTelemetrySnapshot SnapB = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("总借出为35"), SnapB.TotalAcquires, int64(BatchA + BatchB));
    TestEqual(TEXT("复用命中为12"), SnapB.CacheHits, int64(ReleaseA));
    TestEqual(TEXT("新生成穿透为23"), SnapB.CacheMisses, int64(BatchA + (BatchB - ReleaseA)));
    TestEqual(TEXT("当前活跃为23"), SnapB.TotalActiveCount, (BatchA - ReleaseA) + BatchB);
    TestEqual(TEXT("当前空闲为0"), SnapB.TotalIdleCount, 0);
    TestEqual(TEXT("代数守恒：命中加穿透等于总借出"), SnapB.CacheHits + SnapB.CacheMisses, SnapB.TotalAcquires);

    for (AGamePoolableTestActor* Actor : ActiveActors) {
        (void)Subsystem->ReleaseActor(Actor);
    }
    ActiveActors.Empty();

    FPoolTelemetrySnapshot SnapFinal = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("最终全部归还后活跃为0"), SnapFinal.TotalActiveCount, 0);
    TestEqual(TEXT("最终全部归还后空闲为23"), SnapFinal.TotalIdleCount, 23);

    Subsystem->ClearAllPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolTelemetryScopedAndDeferredTest,
    "Game.Pool.Telemetry.ScopedRentalAndDeferredSync",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 RAII 作用域租约与无锁多线程延迟归还路径下的遥测指标同步
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGamePoolTelemetryScopedAndDeferredTest::RunTest([[maybe_unused]] const FString& Parameters) {
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
    Subsystem->ResetTelemetry();

    {
        TGameStatusResult<TScopedPoolRental<AGamePoolableTestActor>> ScopedActor =
            Subsystem->AcquireScopedActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass());
        TestTrue(TEXT("RAII借出Actor成功"), ScopedActor.IsSuccess());

        TGameStatusResult<TScopedPoolRental<UGamePoolableTestObject>> ScopedObj =
            Subsystem->AcquireScopedObject<UGamePoolableTestObject>(UGamePoolableTestObject::StaticClass());
        TestTrue(TEXT("RAII借出Object成功"), ScopedObj.IsSuccess());

        FPoolTelemetrySnapshot InScope = Subsystem->GetTelemetrySnapshot();
        TestEqual(TEXT("作用域内活跃实例为2"), InScope.TotalActiveCount, 2);
        TestEqual(TEXT("作用域内空闲实例为0"), InScope.TotalIdleCount, 0);
        TestEqual(TEXT("作用域内总借出为2"), InScope.TotalAcquires, int64(2));
    }

    FPoolTelemetrySnapshot OutScope = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("跳出作用域后活跃实例自动降为0"), OutScope.TotalActiveCount, 0);
    TestEqual(TEXT("跳出作用域后空闲实例自动升为2"), OutScope.TotalIdleCount, 2);

    TGameStatusResult<AGamePoolableTestActor*> AcqDeferred =
        Subsystem->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass());
    TestTrue(TEXT("借出用于延迟归还的Actor成功"), AcqDeferred.IsSuccess());

    FPoolTelemetrySnapshot BeforeDeferred = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("借出后活跃实例为1"), BeforeDeferred.TotalActiveCount, 1);
    TestEqual(TEXT("借出后空闲实例为1"), BeforeDeferred.TotalIdleCount, 1);

    const bool bEnqueued = Subsystem->ReleaseActorDeferred(AcqDeferred.GetValue()).IsSuccess();
    TestTrue(TEXT("成功压入无锁延迟归还队列"), bEnqueued);

    const int32 Processed = Subsystem->ProcessDeferredReleases(64);
    TestEqual(TEXT("成功收割1个延迟归还实例"), Processed, 1);

    FPoolTelemetrySnapshot AfterDeferred = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("延迟收割后活跃实例归0"), AfterDeferred.TotalActiveCount, 0);
    TestEqual(TEXT("延迟收割后空闲实例恢复为2"), AfterDeferred.TotalIdleCount, 2);

    Subsystem->ClearAllPools();
    return true;
}

#endif
