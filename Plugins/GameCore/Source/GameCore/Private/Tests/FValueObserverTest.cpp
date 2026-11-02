/**
 * @file FValueObserverTest.cpp
 * @brief 通用值观察者测试（订阅即回放 / 启停开关），Shipping 构建自动剔除
 */

#include "Listener/FValueObserver.h"

#if WITH_AUTOMATION_TESTS

#include "Async/Async.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameValueObserverCommitOrderTest, "Game.Core.Observer.NotificationCommitOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证并发值提交捕获各自载荷并严格按提交顺序完成通知
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameValueObserverCommitOrderTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FValueObserver<int32> Observer(FName(TEXT("UnitTest.Observer.NotificationCommitOrder")));
    FEvent* FirstEntered = FPlatformProcess::GetSynchEventFromPool(false);
    FEvent* ReleaseFirst = FPlatformProcess::GetSynchEventFromPool(false);
    FCriticalSection ReceivedMutex;
    TArray<int32> Received;
    const TSharedRef<FObserverSubscription> Subscription = Observer.Subscribe(
        [FirstEntered, ReleaseFirst, &ReceivedMutex, &Received](const int32& Value) {
            if (Value == 1) {
                FirstEntered->Trigger();
                ReleaseFirst->Wait();
            }
            FScopeLock Lock(&ReceivedMutex);
            Received.Add(Value);
        });

    TFuture<void> Writer = Async(EAsyncExecution::Thread, [&Observer]() {
        Observer.Set(1);
    });
    TestTrue(TEXT("首个通知已进入回调"), FirstEntered->Wait(5000));
    Observer.Set(2);
    ReleaseFirst->Trigger();
    Writer.Wait();

    TestEqual(TEXT("两次提交各回调一次"), Received.Num(), 2);
    if (Received.Num() == 2) {
        TestEqual(TEXT("通知一先于通知二完成"), Received[0], 1);
        TestEqual(TEXT("通知二不会被读取为重复值"), Received[1], 2);
    }

    FPlatformProcess::ReturnSynchEventToPool(FirstEntered);
    FPlatformProcess::ReturnSynchEventToPool(ReleaseFirst);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameValueObserverReentrantReplayOrderTest, "Game.Core.Observer.ReentrantReplayOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证回调内订阅回放与再次写入均进入全局 FIFO 而不递归派发
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameValueObserverReentrantReplayOrderTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FValueObserver<int32> Observer(FName(TEXT("UnitTest.Observer.ReentrantReplayOrder")));
    TArray<FString> Events;
    TSharedPtr<FObserverSubscription> SecondSubscription;
    const TSharedRef<FObserverSubscription> FirstSubscription = Observer.Subscribe(
        [&Observer, &Events, &SecondSubscription](const int32& Value) {
            Events.Add(FString::Printf(TEXT("A%d.Begin"), Value));
            if (Value == 1) {
                SecondSubscription = Observer.SubscribeWithReplay([&Events](const int32& ReplayValue) {
                    Events.Add(FString::Printf(TEXT("B%d"), ReplayValue));
                });
                Events.Add(TEXT("A1.AfterSubscribe"));
                Observer.Set(2);
            }
            Events.Add(FString::Printf(TEXT("A%d.End"), Value));
        });

    Observer.Set(1);
    const FString ActualOrder = FString::Join(Events, TEXT("|"));
    const FString ExpectedOrder = TEXT("A1.Begin|A1.AfterSubscribe|A1.End|B1|A2.Begin|A2.End|B2");
    TestEqual(TEXT("重入写入与回放按全局队列顺序派发"), ActualOrder, ExpectedOrder);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
