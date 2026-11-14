/**
 * @file PoolTelemetryTest.cpp
 * @brief GamePool 运行期可观测性与遥测监控 (stat GamePool & Telemetry) 单元测试
 */

#include "ObjectPool/ObjectPoolSubsystem.h"
#include "Tests/GamePoolableTestActor.h"
#include "Tests/UGamePoolableTestObject.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace {

/**
 * @brief 获取测试世界指针
 * @return 当前有效的 UWorld 指针，不存在返回 nullptr
 */
UWorld* GetTestWorld() {
    if (!GEngine) {
        return nullptr;
    }
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World()) {
            return Context.World();
        }
    }
    return nullptr;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolTelemetryHitMissTest,
    "Game.Pool.Telemetry.HitAndMissCalculation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证对象池命中与穿透计数以及命中率计算的准确性
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
    Subsystem->ClearAllObjectPools();
    Subsystem->ResetTelemetry();

    FPoolTelemetrySnapshot InitialSnapshot = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("初始总借出为0"), InitialSnapshot.TotalAcquires, int64(0));
    TestEqual(TEXT("初始命中为0"), InitialSnapshot.CacheHits, int64(0));
    TestEqual(TEXT("初始穿透为0"), InitialSnapshot.CacheMisses, int64(0));
    TestEqual(TEXT("初始命中率为0"), InitialSnapshot.GetHitRatePercentage(), 0.0f);

    Subsystem->Prewarm(AGamePoolableTestActor::StaticClass(), 2);
    FPoolTelemetrySnapshot PrewarmedSnapshot = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("预热后空闲Actor为2"), PrewarmedSnapshot.TotalIdleCount, 2);
    TestEqual(TEXT("预热后借出为0"), PrewarmedSnapshot.TotalAcquires, int64(0));

    TGameStatusResult<AGamePoolableTestActor*> Acq1 = Subsystem->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass());
    TestTrue(TEXT("第1次借出成功"), Acq1.IsSuccess());

    FPoolTelemetrySnapshot Snapshot1 = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("第1次借出后总借出为1"), Snapshot1.TotalAcquires, int64(1));
    TestEqual(TEXT("第1次借出命中为1"), Snapshot1.CacheHits, int64(1));
    TestEqual(TEXT("第1次借出穿透为0"), Snapshot1.CacheMisses, int64(0));
    TestEqual(TEXT("第1次命中率为100%"), Snapshot1.GetHitRatePercentage(), 100.0f);

    TGameStatusResult<AGamePoolableTestActor*> Acq2 = Subsystem->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass());
    TestTrue(TEXT("第2次借出成功"), Acq2.IsSuccess());

    FPoolTelemetrySnapshot Snapshot2 = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("第2次借出后总借出为2"), Snapshot2.TotalAcquires, int64(2));
    TestEqual(TEXT("第2次借出命中为2"), Snapshot2.CacheHits, int64(2));
    TestEqual(TEXT("第2次借出穿透为0"), Snapshot2.CacheMisses, int64(0));
    TestEqual(TEXT("第2次命中率为100%"), Snapshot2.GetHitRatePercentage(), 100.0f);

    TGameStatusResult<AGamePoolableTestActor*> Acq3 = Subsystem->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass());
    TestTrue(TEXT("第3次借出成功（池空新生成）"), Acq3.IsSuccess());

    FPoolTelemetrySnapshot Snapshot3 = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("第3次借出后总借出为3"), Snapshot3.TotalAcquires, int64(3));
    TestEqual(TEXT("第3次借出命中为2"), Snapshot3.CacheHits, int64(2));
    TestEqual(TEXT("第3次借出穿透为1"), Snapshot3.CacheMisses, int64(1));
    const float ExpectedHitRate = (2.0f / 3.0f) * 100.0f;
    TestNearlyEqual(TEXT("3次借出命中率为66.67%"), Snapshot3.GetHitRatePercentage(), ExpectedHitRate, 0.01f);

    if (Acq1.IsSuccess()) {
        Subsystem->ReleaseActor(Acq1.GetValue());
    }
    if (Acq2.IsSuccess()) {
        Subsystem->ReleaseActor(Acq2.GetValue());
    }
    if (Acq3.IsSuccess()) {
        Subsystem->ReleaseActor(Acq3.GetValue());
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
    Subsystem->ClearAllObjectPools();
    Subsystem->ResetTelemetry();

    Subsystem->Prewarm(AGamePoolableTestActor::StaticClass(), 3);
    Subsystem->PrewarmObjectPool(UGamePoolableTestObject::StaticClass(), 2);

    FPoolTelemetrySnapshot InitialSnapshot = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("初始总空闲实例为5 (3 Actor + 2 Object)"), InitialSnapshot.TotalIdleCount, 5);
    TestEqual(TEXT("初始总活跃实例为0"), InitialSnapshot.TotalActiveCount, 0);

    TGameStatusResult<AGamePoolableTestActor*> ActorRes = Subsystem->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass());
    TGameStatusResult<UGamePoolableTestObject*> ObjRes = Subsystem->AcquireObject<UGamePoolableTestObject>(UGamePoolableTestObject::StaticClass());

    TestTrue(TEXT("Actor借出成功"), ActorRes.IsSuccess());
    TestTrue(TEXT("UObject借出成功"), ObjRes.IsSuccess());

    FPoolTelemetrySnapshot MidSnapshot = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("借出后总活跃实例为2"), MidSnapshot.TotalActiveCount, 2);
    TestEqual(TEXT("借出后总空闲实例为3"), MidSnapshot.TotalIdleCount, 3);
    TestEqual(TEXT("借出后总借出次数为2"), MidSnapshot.TotalAcquires, int64(2));
    TestEqual(TEXT("借出后全命中为2"), MidSnapshot.CacheHits, int64(2));

    if (ActorRes.IsSuccess()) {
        Subsystem->ReleaseActor(ActorRes.GetValue());
    }
    if (ObjRes.IsSuccess()) {
        Subsystem->ReleaseObject(ObjRes.GetValue());
    }

    FPoolTelemetrySnapshot FinalSnapshot = Subsystem->GetTelemetrySnapshot();
    TestEqual(TEXT("归还后总活跃实例恢复为0"), FinalSnapshot.TotalActiveCount, 0);
    TestEqual(TEXT("归还后总空闲实例恢复为5"), FinalSnapshot.TotalIdleCount, 5);

    Subsystem->ClearAllPools();
    Subsystem->ClearAllObjectPools();
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
    Subsystem->ClearAllObjectPools();
    Subsystem->ResetTelemetry();

    TGameStatusResult<UGamePoolableTestObject*> ObjRes = Subsystem->AcquireObject<UGamePoolableTestObject>(UGamePoolableTestObject::StaticClass());
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
        Subsystem->ReleaseObject(ObjRes.GetValue());
    }

    Subsystem->ClearAllObjectPools();
    return true;
}

#endif
