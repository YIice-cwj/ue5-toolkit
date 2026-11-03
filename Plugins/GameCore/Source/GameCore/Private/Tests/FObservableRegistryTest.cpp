/**
 * @file FObservableRegistryTest.cpp
 * @brief 任意类型观察者注册表测试（按 Key 注册 / 订阅即回放 / World 作用域），Shipping 构建自动剔除
 */

#include "Listener/FObservableRegistry.h"
#include "System/UObservableRegistrySubsystem.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObservableRegistryTest, "Game.Core.Observable.RegisterAndReplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 注册表：任意类型注册、订阅即回放、Set 广播、注销后不再写入
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FGameObservableRegistryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    Game::Core::FObservableRegistry Registry;
    const FName CritRateKey(TEXT("UnitTest.Observable.CritRate"));
    const FName CooldownKey(TEXT("UnitTest.Observable.Cooldown"));

    TestFalse(TEXT("未注册键写入被拒"), Registry.Set<float>(CritRateKey, 0.25f));

    int32 CritSeenCount = 0;
    float LastCritRate = -1.0f;
    TSharedPtr<FObserverSubscription> CritSubscription = Registry.Subscribe<float>(CritRateKey,
        [&CritSeenCount, &LastCritRate](const float& Value) {
            ++CritSeenCount;
            LastCritRate = Value;
        });
    TestEqual(TEXT("订阅即回放当前值"), CritSeenCount, 1);
    TestNearlyEqual(TEXT("回放值为默认值"), LastCritRate, 0.0f, 0.0001f);
    TestTrue(TEXT("订阅后键已注册"), Registry.Contains(CritRateKey));
    TestEqual(TEXT("注册表键数量为 1"), Registry.Num(), 1);

    TestTrue(TEXT("已注册键写入成功"), Registry.Set<float>(CritRateKey, 0.35f));
    TestEqual(TEXT("写入后收到一次更新"), CritSeenCount, 2);
    TestNearlyEqual(TEXT("更新值正确"), LastCritRate, 0.35f, 0.0001f);

    TestFalse(TEXT("未注册整数键写入被拒"), Registry.Set<int32>(CooldownKey, 3));
    [[maybe_unused]] FValueObserver<int32>* CooldownObserver = Registry.Register<int32>(CooldownKey);
    TestNotNull(TEXT("整数观察者注册成功"), CooldownObserver);
    TestTrue(TEXT("整数键写入成功"), Registry.Set<int32>(CooldownKey, 3));
    TestEqual(TEXT("两个不同类型的键共存"), Registry.Num(), 2);

    CritSubscription.Reset();
    TestTrue(TEXT("注销键成功"), Registry.Unregister(CooldownKey));
    TestFalse(TEXT("注销后键不存在"), Registry.Contains(CooldownKey));
    Registry.Reset();
    TestEqual(TEXT("Reset 后注册表为空"), Registry.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObservableOwnerBoundTest, "Game.Core.Observable.OwnerBound",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 注册表：Owner 存活收到回调、Owner 销毁后广播跳过且条目被惰性清理、旧订阅 API 不受影响
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FGameObservableOwnerBoundTest::RunTest([[maybe_unused]] const FString& Parameters) {
    Game::Core::FObservableRegistry Registry;
    const FName Key(TEXT("UnitTest.Observable.OwnerBound"));
    UObject* Owner = NewObject<UGameInstance>();

    int32 OwnerSeenCount = 0;
    TSharedPtr<FObserverSubscription> OwnerSubscription = Registry.Subscribe<int32>(Owner, Key,
        [&OwnerSeenCount](const int32&) {
            ++OwnerSeenCount;
        });
    TestEqual(TEXT("Owner 订阅即回放当前值"), OwnerSeenCount, 1);

    int32 PlainSeenCount = 0;
    TSharedPtr<FObserverSubscription> PlainSubscription = Registry.Subscribe<int32>(Key,
        [&PlainSeenCount](const int32&) {
            ++PlainSeenCount;
        });
    TestEqual(TEXT("无 Owner 订阅即回放当前值"), PlainSeenCount, 1);

    TestTrue(TEXT("写入成功"), Registry.Set<int32>(Key, 5));
    TestEqual(TEXT("Owner 存活时收到更新"), OwnerSeenCount, 2);
    TestEqual(TEXT("无 Owner 订阅收到更新"), PlainSeenCount, 2);

    TSharedPtr<FValueObserver<int32>> Observer = Registry.Find<int32>(Key);
    TestTrue(TEXT("观察者有效"), Observer.IsValid());
    if (!Observer.IsValid()) {
        return false;
    }
    TestEqual(TEXT("清理前有效条目为 2"), Observer->NumListeners(), 2);

    Owner->MarkAsGarbage();
    TestTrue(TEXT("Owner 销毁后写入仍成功"), Registry.Set<int32>(Key, 9));
    TestEqual(TEXT("Owner 销毁后不再回调"), OwnerSeenCount, 2);
    TestEqual(TEXT("无 Owner 订阅继续收到更新"), PlainSeenCount, 3);
    TestEqual(TEXT("失效条目被惰性清理"), Observer->NumListeners(), 1);

    UObject* DeadOwner = NewObject<UGameInstance>();
    DeadOwner->MarkAsGarbage();
    int32 DeadSeenCount = 0;
    TSharedPtr<FObserverSubscription> DeadSubscription = Registry.Subscribe<int32>(DeadOwner, Key,
        [&DeadSeenCount](const int32&) {
            ++DeadSeenCount;
        });
    TestEqual(TEXT("订阅时 Owner 已失效不回放"), DeadSeenCount, 0);
    TestEqual(TEXT("已失效 Owner 的条目同样被清理"), Observer->NumListeners(), 1);

    OwnerSubscription.Reset();
    PlainSubscription.Reset();
    DeadSubscription.Reset();
    TestEqual(TEXT("全部退订后条目为零"), Observer->NumListeners(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObservableWorldSubsystemTest, "Game.Core.Observable.WorldSubsystem",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief World 作用域：世界内可取得注册表子系统，静态 Get 与实例访问一致
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FGameObservableWorldSubsystemTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("ObservableRegistryTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("临时世界有效"), World);
    if (World == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    UObservableRegistrySubsystem* Subsystem = World->GetSubsystem<UObservableRegistrySubsystem>();
    TestNotNull(TEXT("世界注册表子系统有效"), Subsystem);
    if (Subsystem == nullptr) {
        GameInstance->Shutdown();
        return false;
    }
    TestEqual(TEXT("静态 Get 返回同一子系统"), UObservableRegistrySubsystem::Get(World), Subsystem);

    Game::Core::FObservableRegistry& Registry = Subsystem->GetRegistry();
    int32 SeenTotal = 0;
    const FName DamageKey(TEXT("UnitTest.Observable.WorldDamage"));
    TSharedPtr<FObserverSubscription> Subscription = Registry.Subscribe<int32>(DamageKey,
        [&SeenTotal](const int32& Value) {
            SeenTotal += Value;
        });
    TestTrue(TEXT("World 注册表写入成功"), Registry.Set<int32>(DamageKey, 7));
    TestEqual(TEXT("World 注册表广播生效"), SeenTotal, 7);
    Subscription.Reset();

    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObservableErrorBranchesTest, "Game.Core.Observable.ErrorBranches",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证空键、类型冲突、未注册键查找与重复注销的异常分支防御行为
 * @param Parameters 框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameObservableErrorBranchesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    Game::Core::FObservableRegistry Registry;

    TestNull(TEXT("空键注册返回 nullptr"), Registry.Register<int32>(NAME_None));
    TestFalse(TEXT("空键注销返回 false"), Registry.Unregister(NAME_None));
    TestNull(TEXT("未注册键查找返回空共享指针"), Registry.Find<int32>(FName(TEXT("NonExistentKey"))).Get());

    const TSharedRef<FObserverSubscription> EmptyKeySub = Registry.Subscribe<int32>(NAME_None, [](const int32&) {});
    TestFalse(TEXT("空键订阅返回空守卫"), EmptyKeySub->IsActive());

    const FName TypedKey(TEXT("UnitTest.Observable.TypeConflictKey"));
    TestNotNull(TEXT("首次注册 float 观察者成功"), Registry.Register<float>(TypedKey));

    AddExpectedErrorPlain(TEXT("已注册为其他值类型"), EAutomationExpectedErrorFlags::Contains, 2);
    TestNull(TEXT("同名键以 int32 重复注册被拒绝并返回 nullptr"), Registry.Register<int32>(TypedKey));
    TestNull(TEXT("以错误类型查找返回空共享指针"), Registry.Find<int32>(TypedKey).Get());

    const TSharedRef<FObserverSubscription> MismatchedSub = Registry.Subscribe<int32>(TypedKey, [](const int32&) {});
    TestFalse(TEXT("同名键以错误类型订阅返回空守卫"), MismatchedSub->IsActive());

    TestFalse(TEXT("注销不存在的键返回 false"), Registry.Unregister(FName(TEXT("NonExistentKey"))));
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
