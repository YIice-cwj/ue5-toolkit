/**
 * @file PoolEvictionTest.cpp
 * @brief GamePool TTL 冷数据平滑淘汰、保底留存、时间戳刷新与 UThrottledTicker 自动心跳调度自动化测试
 */

#include "Subsystem/UObjectPoolSubsystem.h"

#include "Async/UThrottledTickerSubsystem.h"
#include "Tests/AGamePoolableTestActor.h"
#include "Tests/UGamePoolableTestObject.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/GamePoolTestUtils.h"

using Game::Pool::TestUtils::GetTestWorld;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolEvictionTTLAndMinPreservedTest,
    "Game.Pool.Eviction.TTLAndMinPreserved",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Actor 对象池基于生存期 (TTL) 的冷数据淘汰与保底留存数量 (MinPreservedCount) 约束
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGamePoolEvictionTTLAndMinPreservedTest::RunTest([[maybe_unused]] const FString& Parameters) {
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
    const TSubclassOf<AActor> ActorClass = AGamePoolableTestActor::StaticClass();

    Subsystem->Prewarm(ActorClass, 5);
    TestEqual(TEXT("初始预热 5 个空闲实例"), Subsystem->GetPoolStats(ActorClass).IdleCount, 5);

    FPoolEvictionSettings EvictionSettings;
    EvictionSettings.bEnableEviction = true;
    EvictionSettings.IdleTimeToLiveSeconds = 10.0f;
    EvictionSettings.MinPreservedCount = 2;
    Subsystem->SetActorPoolEvictionSettings(ActorClass, EvictionSettings);

    const double BaseTime = FPlatformTime::Seconds();
    TestEqual(TEXT("未达到 TTL 时不应淘汰任何实例"), Subsystem->ForceEvictExpired(32, BaseTime), 0);
    TestEqual(TEXT("闲置 5 秒仍未达 10 秒 TTL，不应淘汰任何实例"), Subsystem->ForceEvictExpired(32, BaseTime + 5.0), 0);
    TestEqual(TEXT("达到 TTL 后应平滑淘汰超出保底数量的实例"), Subsystem->ForceEvictExpired(32, BaseTime + 15.0), 3);
    TestEqual(TEXT("淘汰后空闲数应等于保底保留数"), Subsystem->GetPoolStats(ActorClass).IdleCount, 2);
    TestEqual(TEXT("处于保底数量内即使进一步超时也不应继续淘汰"), Subsystem->ForceEvictExpired(32, BaseTime + 30.0), 0);

    Subsystem->SetActorPoolEvictionSettings(ActorClass, FPoolEvictionSettings{});
    Subsystem->ClearAllPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolEvictionMaxPerStepTest,
    "Game.Pool.Eviction.MaxEvictionsPerStep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证单步平滑销毁配额严格按步平摊销毁开销
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGamePoolEvictionMaxPerStepTest::RunTest([[maybe_unused]] const FString& Parameters) {
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
    const TSubclassOf<AActor> ActorClass = AGamePoolableTestActor::StaticClass();

    Subsystem->Prewarm(ActorClass, 6);
    TestEqual(TEXT("预热 6 个待淘汰实例"), Subsystem->GetPoolStats(ActorClass).IdleCount, 6);

    FPoolEvictionSettings EvictionSettings;
    EvictionSettings.bEnableEviction = true;
    EvictionSettings.IdleTimeToLiveSeconds = 5.0f;
    EvictionSettings.MinPreservedCount = 0;
    EvictionSettings.MaxEvictionsPerStep = 2;
    Subsystem->SetActorPoolEvictionSettings(ActorClass, EvictionSettings);

    const double SimulatedTime = FPlatformTime::Seconds() + 20.0;
    TestEqual(TEXT("第 1 步应严格受限销毁 2 个实例"), Subsystem->ForceEvictExpired(2, SimulatedTime), 2);
    TestEqual(TEXT("第 1 步后剩余 4 个空闲实例"), Subsystem->GetPoolStats(ActorClass).IdleCount, 4);
    TestEqual(TEXT("第 2 步应严格受限销毁 2 个实例"), Subsystem->ForceEvictExpired(2, SimulatedTime), 2);
    TestEqual(TEXT("第 2 步后剩余 2 个空闲实例"), Subsystem->GetPoolStats(ActorClass).IdleCount, 2);
    TestEqual(TEXT("第 3 步应销毁剩余最后 2 个实例"), Subsystem->ForceEvictExpired(2, SimulatedTime), 2);
    TestEqual(TEXT("池应已完全清空"), Subsystem->GetPoolStats(ActorClass).IdleCount, 0);
    TestEqual(TEXT("空池再次淘汰返回 0"), Subsystem->ForceEvictExpired(2, SimulatedTime), 0);

    Subsystem->SetActorPoolEvictionSettings(ActorClass, FPoolEvictionSettings{});
    Subsystem->ClearAllPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolEvictionRefreshTTLTest,
    "Game.Pool.Eviction.ReacquireRefreshTTL",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证重新借还刷新时间戳后，旧冷实例先被 FIFO 逐出而刷新热实例在 LIFO 端幸存
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGamePoolEvictionRefreshTTLTest::RunTest([[maybe_unused]] const FString& Parameters) {
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
    const TSubclassOf<AActor> ActorClass = AGamePoolableTestActor::StaticClass();

    Subsystem->Prewarm(ActorClass, 2);
    const double TimeAfterPrewarm = FPlatformTime::Seconds();

    FPoolEvictionSettings EvictionSettings;
    EvictionSettings.bEnableEviction = true;
    EvictionSettings.IdleTimeToLiveSeconds = 10.0f;
    EvictionSettings.MinPreservedCount = 0;
    Subsystem->SetActorPoolEvictionSettings(ActorClass, EvictionSettings);

    AGamePoolableTestActor* HotActor = Subsystem->AcquireActor<AGamePoolableTestActor>(ActorClass).GetValue();
    TestNotNull(TEXT("借出热实例成功"), HotActor);
    if (!HotActor) {
        return false;
    }
    TestTrue(TEXT("归还热实例刷新时间戳成功"), Subsystem->ReleaseActor(HotActor).IsSuccess());

    TestEqual(TEXT("仅逐出 1 个最冷实例后热实例仍幸存"), Subsystem->ForceEvictExpired(1, TimeAfterPrewarm + 15.0), 1);
    TestEqual(TEXT("剩余空闲数为 1"), Subsystem->GetPoolStats(ActorClass).IdleCount, 1);

    AGamePoolableTestActor* SurvivingActor = Subsystem->AcquireActor<AGamePoolableTestActor>(ActorClass).GetValue();
    TestEqual(TEXT("幸存者精确为后归还的热实例"), SurvivingActor, HotActor);
    TestTrue(TEXT("归还幸存实例成功"), Subsystem->ReleaseActor(SurvivingActor).IsSuccess());

    Subsystem->SetActorPoolEvictionSettings(ActorClass, FPoolEvictionSettings{});
    Subsystem->ClearAllPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolEvictionUObjectTest,
    "Game.Pool.Eviction.UObjectTTL",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证非 Actor 纯 UObject 对象池同样支持精准平滑 TTL 缩容与保底留存
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGamePoolEvictionUObjectTest::RunTest([[maybe_unused]] const FString& Parameters) {
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
    const TSubclassOf<UObject> ObjectClass = UGamePoolableTestObject::StaticClass();

    Subsystem->PrewarmObjectPool(ObjectClass, 4);
    TestEqual(TEXT("初始预热 4 个空闲 UObject"), Subsystem->GetObjectPoolStats(ObjectClass).IdleCount, 4);

    FPoolEvictionSettings EvictionSettings;
    EvictionSettings.bEnableEviction = true;
    EvictionSettings.IdleTimeToLiveSeconds = 8.0f;
    EvictionSettings.MinPreservedCount = 1;
    Subsystem->SetObjectPoolEvictionSettings(ObjectClass, EvictionSettings);

    const double BaseTime = FPlatformTime::Seconds();
    TestEqual(TEXT("4 秒时未达到 8 秒 TTL，不应淘汰任何 UObject"), Subsystem->ForceEvictExpired(32, BaseTime + 4.0), 0);
    TestEqual(TEXT("12 秒超时后应淘汰 3 个 UObject（保留 1 个保底）"), Subsystem->ForceEvictExpired(32, BaseTime + 12.0), 3);
    TestEqual(TEXT("剩余空闲 UObject 应为 1"), Subsystem->GetObjectPoolStats(ObjectClass).IdleCount, 1);

    Subsystem->SetObjectPoolEvictionSettings(ObjectClass, FPoolEvictionSettings{});
    Subsystem->ClearAllPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolEvictionAutoScheduleTest,
    "Game.Pool.Eviction.AutoScheduleAndDynamicInterval",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 确定性验证后台节流心跳自动淘汰尊重单池 MaxEvictionsPerStep、动态取最小心跳周期及全关自动停机
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGamePoolEvictionAutoScheduleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Subsystem = World->GetSubsystem<UObjectPoolSubsystem>();
    UThrottledTickerSubsystem* Ticker = World->GetSubsystem<UThrottledTickerSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Subsystem);
    TestNotNull(TEXT("节流心跳子系统有效"), Ticker);
    if (!Subsystem || !Ticker) {
        return false;
    }

    const TSubclassOf<AActor> ActorClass = AGamePoolableTestActor::StaticClass();
    const TSubclassOf<UObject> ObjectClass = UGamePoolableTestObject::StaticClass();
    Subsystem->SetActorPoolEvictionSettings(ActorClass, FPoolEvictionSettings{});
    Subsystem->SetObjectPoolEvictionSettings(ObjectClass, FPoolEvictionSettings{});
    Subsystem->ClearAllPools();

    const int32 BaselineTickCount = Ticker->GetRegisteredCount();

    Subsystem->Prewarm(ActorClass, 6);
    Subsystem->PrewarmObjectPool(ObjectClass, 5);

    FPoolEvictionSettings ActorEvict;
    ActorEvict.bEnableEviction = true;
    ActorEvict.IdleTimeToLiveSeconds = 0.0f;
    ActorEvict.MinPreservedCount = 1;
    ActorEvict.CheckIntervalSeconds = 2.0f;
    ActorEvict.MaxEvictionsPerStep = 2;
    Subsystem->SetActorPoolEvictionSettings(ActorClass, ActorEvict);
    TestEqual(TEXT("启用淘汰后自动注册 1 个节流心跳"), Ticker->GetRegisteredCount(), BaselineTickCount + 1);

    FPoolEvictionSettings ObjEvict;
    ObjEvict.bEnableEviction = true;
    ObjEvict.IdleTimeToLiveSeconds = 0.0f;
    ObjEvict.MinPreservedCount = 1;
    ObjEvict.CheckIntervalSeconds = 0.5f;
    ObjEvict.MaxEvictionsPerStep = 1;
    Subsystem->SetObjectPoolEvictionSettings(ObjectClass, ObjEvict);
    TestEqual(TEXT("多池启用淘汰复用同一节流心跳并动态下调至最小周期"), Ticker->GetRegisteredCount(), BaselineTickCount + 1);

    Ticker->Tick(0.6f);
    TestEqual(TEXT("首次自动心跳严格尊重 Actor 池 MaxEvictionsPerStep=2（6->4）"), Subsystem->GetPoolStats(ActorClass).IdleCount, 4);
    TestEqual(TEXT("首次自动心跳严格尊重 Object 池 MaxEvictionsPerStep=1（5->4）"), Subsystem->GetObjectPoolStats(ObjectClass).IdleCount, 4);

    Ticker->Tick(0.6f);
    TestEqual(TEXT("二次自动心跳继续平滑缩容 Actor（4->2）"), Subsystem->GetPoolStats(ActorClass).IdleCount, 2);
    TestEqual(TEXT("二次自动心跳继续平滑缩容 Object（4->3）"), Subsystem->GetObjectPoolStats(ObjectClass).IdleCount, 3);

    Subsystem->SetActorPoolEvictionSettings(ActorClass, FPoolEvictionSettings{});
    TestEqual(TEXT("仅关闭 Actor 池淘汰时心跳仍在为 Object 池运行"), Ticker->GetRegisteredCount(), BaselineTickCount + 1);

    Subsystem->SetObjectPoolEvictionSettings(ObjectClass, FPoolEvictionSettings{});
    TestEqual(TEXT("全部池关闭淘汰后自动注销节流心跳实现零后台空转"), Ticker->GetRegisteredCount(), BaselineTickCount);

    Subsystem->ClearAllPools();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
