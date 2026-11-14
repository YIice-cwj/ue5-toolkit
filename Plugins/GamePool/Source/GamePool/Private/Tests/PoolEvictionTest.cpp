/**
 * @file PoolEvictionTest.cpp
 * @brief GamePool 弹性缩容与冷数据分帧淘汰 (TTL Eviction) 单元测试
 */

#include "ObjectPool/ObjectPoolSubsystem.h"
#include "Tests/GamePoolableTestActor.h"
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
    FPoolStats StatsInitial = Subsystem->GetPoolStats(ActorClass);
    TestEqual(TEXT("初始预热 5 个空闲实例"), StatsInitial.IdleCount, 5);

    FPoolEvictionSettings EvictionSettings;
    EvictionSettings.bEnableEviction = true;
    EvictionSettings.IdleTimeToLiveSeconds = 10.0f;
    EvictionSettings.MinPreservedCount = 2;
    Subsystem->SetActorPoolEvictionSettings(ActorClass, EvictionSettings);

    const double BaseTime = FPlatformTime::Seconds();
    const int32 ImmediateEvicted = Subsystem->ForceEvictExpired(32, BaseTime);
    TestEqual(TEXT("未达到 TTL 时不应淘汰任何实例"), ImmediateEvicted, 0);

    const int32 PartialTimeEvicted = Subsystem->ForceEvictExpired(32, BaseTime + 5.0);
    TestEqual(TEXT("闲置 5 秒仍未达 10 秒 TTL，不应淘汰任何实例"), PartialTimeEvicted, 0);

    const int32 OverdueEvicted = Subsystem->ForceEvictExpired(32, BaseTime + 15.0);
    TestEqual(TEXT("达到 TTL 后应平滑淘汰超出保底数量的实例"), OverdueEvicted, 3);

    FPoolStats StatsAfterEvict = Subsystem->GetPoolStats(ActorClass);
    TestEqual(TEXT("淘汰后空闲数应等于保底保留数"), StatsAfterEvict.IdleCount, 2);

    const int32 ExtraCheckEvicted = Subsystem->ForceEvictExpired(32, BaseTime + 30.0);
    TestEqual(TEXT("处于保底数量内即使进一步超时也不应继续淘汰"), ExtraCheckEvicted, 0);

    Subsystem->ClearAllPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolEvictionMaxPerStepTest,
    "Game.Pool.Eviction.MaxEvictionsPerStep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证单步平滑销毁配额 (MaxEvictionsPerStep) 严格按帧平摊销毁开销
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

    const int32 Step1Evicted = Subsystem->ForceEvictExpired(2, SimulatedTime);
    TestEqual(TEXT("第 1 步应严格受限销毁 2 个实例"), Step1Evicted, 2);
    TestEqual(TEXT("第 1 步后剩余 4 个空闲实例"), Subsystem->GetPoolStats(ActorClass).IdleCount, 4);

    const int32 Step2Evicted = Subsystem->ForceEvictExpired(2, SimulatedTime);
    TestEqual(TEXT("第 2 步应严格受限销毁 2 个实例"), Step2Evicted, 2);
    TestEqual(TEXT("第 2 步后剩余 2 个空闲实例"), Subsystem->GetPoolStats(ActorClass).IdleCount, 2);

    const int32 Step3Evicted = Subsystem->ForceEvictExpired(2, SimulatedTime);
    TestEqual(TEXT("第 3 步应销毁剩余最后 2 个实例"), Step3Evicted, 2);
    TestEqual(TEXT("池应已完全清空"), Subsystem->GetPoolStats(ActorClass).IdleCount, 0);

    const int32 Step4Evicted = Subsystem->ForceEvictExpired(2, SimulatedTime);
    TestEqual(TEXT("空池再次淘汰返回 0"), Step4Evicted, 0);

    Subsystem->ClearAllPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolEvictionRefreshTTLTest,
    "Game.Pool.Eviction.ReacquireRefreshTTL",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证重新借还后归还时间戳正确刷新，未超时热实例不被误杀
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
    const double BaseTime = FPlatformTime::Seconds();

    FPoolEvictionSettings EvictionSettings;
    EvictionSettings.bEnableEviction = true;
    EvictionSettings.IdleTimeToLiveSeconds = 10.0f;
    EvictionSettings.MinPreservedCount = 0;
    Subsystem->SetActorPoolEvictionSettings(ActorClass, EvictionSettings);

    const TGameStatusResult<AGamePoolableTestActor*> AcquireResult =
        Subsystem->AcquireActor<AGamePoolableTestActor>(ActorClass);
    TestTrue(TEXT("借出一个实例成功"), AcquireResult.IsSuccess());
    AGamePoolableTestActor* ReusedActor = AcquireResult.GetValue();

    const TGameStatusResult<void> ReleaseResult = Subsystem->ReleaseActor(ReusedActor);
    TestTrue(TEXT("重新归还实例成功"), ReleaseResult.IsSuccess());

    const int32 EvictedAtBorder = Subsystem->ForceEvictExpired(32, BaseTime + 8.0);
    TestEqual(TEXT("在第 8 秒时两个实例均未达到 10 秒 TTL"), EvictedAtBorder, 0);

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

    const int32 PrematureEvicted = Subsystem->ForceEvictExpired(32, BaseTime + 4.0);
    TestEqual(TEXT("4 秒时未达到 8 秒 TTL，不应淘汰任何 UObject"), PrematureEvicted, 0);

    const int32 ExpiredEvicted = Subsystem->ForceEvictExpired(32, BaseTime + 12.0);
    TestEqual(TEXT("12 秒超时后应淘汰 3 个 UObject（保留 1 个保底）"), ExpiredEvicted, 3);
    TestEqual(TEXT("剩余空闲 UObject 应为 1"), Subsystem->GetObjectPoolStats(ObjectClass).IdleCount, 1);

    Subsystem->ClearAllPools();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
