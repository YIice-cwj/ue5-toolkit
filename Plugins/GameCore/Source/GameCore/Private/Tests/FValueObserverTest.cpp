/**
 * @file FValueObserverTest.cpp
 * @brief 通用值观察者测试（订阅即回放 / 启停开关），Shipping 构建自动剔除
 */

#include "Listener/FValueObserver.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameValueObserverReplayTest, "Game.Core.Observer.ReplayAndToggle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 订阅即得当前值、启停开关静默跳过广播、守卫析构注销
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FGameValueObserverReplayTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FValueObserver<int32> Observer(FName(TEXT("UnitTest.Observer.ReplayToggle")));
    Observer.Set(5);

    TArray<int32> Received;
    TSharedPtr<FObserverSubscription> Subscription = Observer.SubscribeWithReplay(
        [&Received](const int32& Value) {
            Received.Add(Value);
        });
    TestEqual(TEXT("订阅即回放当前值"), Received.Num(), 1);
    TestEqual(TEXT("回放值等于当前值"), Received[0], 5);
    TestEqual(TEXT("Get 返回当前值副本"), Observer.Get(), 5);

    Observer.Set(7);
    TestEqual(TEXT("更新后收到新值"), Received.Num(), 2);
    TestEqual(TEXT("更新值正确"), Received[1], 7);

    Observer.SetEnabled(false);
    Observer.Set(9);
    TestEqual(TEXT("禁用后广播被静默跳过"), Received.Num(), 2);

    FConfigRegistry::Get().SetConfig<bool>(FName(TEXT("Listener.UnitTest.Observer.ReplayToggle.Enabled")), true);
    TestTrue(TEXT("通过 FConfigRegistry 热更新可恢复启用态"), Observer.IsEnabled());
    Observer.Set(11);
    TestEqual(TEXT("重新启用后广播恢复"), Received.Num(), 3);
    if (Received.Num() >= 3) {
        TestEqual(TEXT("重新启用后的值正确"), Received[2], 11);
    }

    Subscription.Reset();
    Observer.Set(13);
    TestEqual(TEXT("守卫析构后不再收到广播"), Received.Num(), 3);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
