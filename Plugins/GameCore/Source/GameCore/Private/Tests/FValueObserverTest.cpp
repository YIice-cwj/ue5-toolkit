/**
 * @file FValueObserverTest.cpp
 * @brief 通用值观察者测试（订阅即回放 / 启停开关），Shipping 构建自动剔除
 */

#include "Listener/FValueObserver.h"
#include "Listener/FListenerHub.h"

#if WITH_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameValueObserverReplayTest, "Game.Core.Listener.Observer.ReplayAndToggle",
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameValueObserverCommitOrderTest, "Game.Core.Listener.Observer.NotificationCommitOrder",
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameValueObserverReentrantReplayOrderTest, "Game.Core.Listener.Observer.ReentrantReplayOrder",
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameValueObserverOwnerLifecycleAndInitialValueTest,
    "Game.Core.Listener.Observer.OwnerLifecycleAndInitialValue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证初始值构造、版本号、空回调过滤以及 UObject 宿主生命周期感知与惰性清理
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameValueObserverOwnerLifecycleAndInitialValueTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FValueObserver<int32> Observer(FName(TEXT("UnitTest.Observer.OwnerLifecycle")), 42);
    TestEqual(TEXT("带初始值构造读取值正确"), Observer.Get(), 42);
    TestEqual(TEXT("初始版本号为 0"), Observer.GetVersion(), static_cast<uint32>(0));

    const TSharedRef<FObserverSubscription> NullSub = Observer.Subscribe(nullptr);
    TestFalse(TEXT("空回调订阅返回非活跃句柄"), NullSub->IsActive());

    const TSharedRef<FObserverSubscription> NullReplaySub = Observer.SubscribeWithReplay(nullptr);
    TestFalse(TEXT("空回调带回放订阅返回非活跃句柄"), NullReplaySub->IsActive());

    UGameInstance* Owner = NewObject<UGameInstance>();
    TestNotNull(TEXT("测试宿主对象有效创建"), Owner);

    int32 OwnerReceived = 0;
    const TSharedRef<FObserverSubscription> OwnerSub = Observer.SubscribeWithReplay(
        Owner,
        [&OwnerReceived](const int32& Value) {
            OwnerReceived = Value;
        }
    );

    TestTrue(TEXT("有效订阅返回活跃句柄"), OwnerSub->IsActive());
    TestEqual(TEXT("带有效 Owner 订阅立即收到初始回放"), OwnerReceived, 42);
    TestEqual(TEXT("活跃监听条目数为 1"), Observer.NumListeners(), 1);

    Observer.Set(88);
    TestEqual(TEXT("Owner 存活期间正常收到新值广播"), OwnerReceived, 88);
    TestEqual(TEXT("值提交后版本号递增为 1"), Observer.GetVersion(), static_cast<uint32>(1));

    Owner->MarkAsGarbage();
    TestFalse(TEXT("宿主标记垃圾回收后弱引用失效"), IsValid(Owner));

    Observer.Set(99);
    TestEqual(TEXT("宿主垃圾回收后回调被安全跳过"), OwnerReceived, 88);
    TestEqual(TEXT("Purge 自动清理失效监听条目总数归零"), Observer.NumListeners(), 0);

    ObserverSub->Reset();
    TestFalse(TEXT("句柄 Reset 后处于非活跃状态"), OwnerSub->IsActive());
    OwnerSub->Reset();
    TestFalse(TEXT("句柄重复 Reset 保证幂等安全"), OwnerSub->IsActive());

    TestFalse(TEXT("无订阅者时 HasListeners 为 false"), Observer.HasListeners());
    int32 UpdatedCount = 0;
    const TSharedRef<FObserverSubscription> ActiveSub = Observer.Subscribe([&UpdatedCount](const int32& NewVal) {
        UpdatedCount = NewVal;
    });
    TestTrue(TEXT("订阅后 HasListeners 为 true"), Observer.HasListeners());
    TestTrue(TEXT("GetSubscriptionId 大于 0"), ActiveSub->GetSubscriptionId() > 0);

    Observer.Update([](int32& Val) {
        Val += 100;
    });
    TestEqual(TEXT("Update 成功原位变换当前值"), Observer.Get(), 199);
    TestEqual(TEXT("Update 正常触发通知"), UpdatedCount, 199);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameListenerHubTest,
    "Game.Core.Listener.ListenerHub.FullLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FListenerHub 事件总线全生命周期（注册、广播、COW 多监听、类型指纹防伪、退订与重置）
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameListenerHubTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FListenerHub& Hub = FListenerHub::Get();
    Hub.Reset();

    TestTrue(TEXT("初始状态事件总线为空"), Hub.IsEmpty());
    TestEqual(TEXT("初始事件通道数为 0"), Hub.Num(), 0);

    const FName TestEvent(TEXT("UnitTest.Event.PlayerDamaged"));
    TestFalse(TEXT("未注册事件 Contains 返回 false"), Hub.Contains(TestEvent));
    TestFalse(TEXT("未注册事件 HasListeners 返回 false"), Hub.HasListeners(TestEvent));
    TestEqual(TEXT("未注册事件 NumListeners 返回 0"), Hub.NumListeners(TestEvent), 0);

    TestFalse(TEXT("未注册事件 Emit 安全返回 false"), Hub.Emit(TestEvent, 100));
    TestFalse(TEXT("NAME_None 事件 Emit 安全返回 false"), Hub.Emit(NAME_None, 100));

    const TSharedRef<FObserverSubscription> NoneSub = Hub.Subscribe<int32>(NAME_None, [](const int32&) {});
    TestFalse(TEXT("NAME_None 事件 Subscribe 返回非活跃句柄"), NoneSub->IsActive());

    int32 Listener1Val = 0;
    int32 Listener2Val = 0;

    TSharedPtr<FObserverSubscription> Sub1 = Hub.Subscribe<int32>(TestEvent, [&Listener1Val](const int32& Damage) {
        Listener1Val = Damage;
    });

    TestTrue(TEXT("有效注册后 Contains 返回 true"), Hub.Contains(TestEvent));
    TestFalse(TEXT("注册后总线非空"), Hub.IsEmpty());
    TestEqual(TEXT("事件通道数为 1"), Hub.Num(), 1);
    TestTrue(TEXT("HasListeners 返回 true"), Hub.HasListeners(TestEvent));
    TestEqual(TEXT("NumListeners 为 1"), Hub.NumListeners(TestEvent), 1);
    TestTrue(TEXT("Sub1 处于活跃状态"), Sub1->IsActive());
    TestTrue(TEXT("Sub1 订阅 ID 有效"), Sub1->GetSubscriptionId() > 0);

    TSharedPtr<FObserverSubscription> Sub2 = Hub.Subscribe<int32>(TestEvent, [&Listener2Val](const int32& Damage) {
        Listener2Val = Damage;
    });
    TestEqual(TEXT("追加订阅后 NumListeners 为 2"), Hub.NumListeners(TestEvent), 2);

    TestTrue(TEXT("成功触发事件广播"), Hub.Emit(TestEvent, 50));
    TestEqual(TEXT("监听者 1 收到载荷"), Listener1Val, 50);
    TestEqual(TEXT("监听者 2 收到载荷"), Listener2Val, 50);

    TestFalse(TEXT("不同载荷类型 Emit 被类型指纹安全拦截"), Hub.Emit(TestEvent, FString(TEXT("NotAnInt"))));

    const TSharedRef<FObserverSubscription> MismatchSub = Hub.Subscribe<FString>(TestEvent, [](const FString&) {});
    TestFalse(TEXT("不同载荷类型 Subscribe 被类型指纹拦截返回非活跃句柄"), MismatchSub->IsActive());
    TestEqual(TEXT("拦截后 NumListeners 依然为 2"), Hub.NumListeners(TestEvent), 2);

    Sub1->Reset();
    TestFalse(TEXT("Sub1 退订后处于非活跃状态"), Sub1->IsActive());
    TestEqual(TEXT("退订后 NumListeners 降为 1"), Hub.NumListeners(TestEvent), 1);

    TestTrue(TEXT("再次触发广播"), Hub.Emit(TestEvent, 80));
    TestEqual(TEXT("已退订监听者 1 不再更新"), Listener1Val, 50);
    TestEqual(TEXT("活跃监听者 2 收到新载荷"), Listener2Val, 80);

    Sub2.Reset();
    TestEqual(TEXT("所有监听者退订后 NumListeners 为 0"), Hub.NumListeners(TestEvent), 0);
    TestFalse(TEXT("无监听者时 HasListeners 为 false"), Hub.HasListeners(TestEvent));

    Hub.Reset();
    TestTrue(TEXT("Reset 后总线恢复为空"), Hub.IsEmpty());
    TestEqual(TEXT("Reset 后通道数归零"), Hub.Num(), 0);
    TestFalse(TEXT("Reset 后原通道不再存在"), Hub.Contains(TestEvent));

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
