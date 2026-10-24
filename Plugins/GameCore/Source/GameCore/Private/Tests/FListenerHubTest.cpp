/**
 * @file FListenerHubTest.cpp
 * @brief 监听总线模块内自动化测试（COW 快照遍历隔离 / 退订按 ID / 类型指纹分流），Shipping 构建自动剔除
 */

#include "Listener/FListenerHub.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace {

/** 测试用事件载荷 */
struct FUnitTestEventPayload {
    int32 Value = 0;
};

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameListenerHubSnapshotTest, "Game.Core.Listener.HubSnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief COW 快照：回调内订阅不影响当轮遍历，退订按订阅 ID 生效
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 *
 * 实现思路：首订阅者在回调内追加第二订阅者，断言当轮仍只有首订阅者收到；
 * 再经守卫 Reset 退订首订阅者，断言仅第二订阅者继续收到
 */
bool FGameListenerHubSnapshotTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FListenerHub& Hub = FListenerHub::Get();
    static const FName EventName(TEXT("UnitTest.Listener.HubSnapshot"));

    int32 FirstSeenSum = 0;
    int32 SecondSeenSum = 0;
    TSharedPtr<FObserverSubscription> SecondSubscription;

    const TSharedRef<FObserverSubscription> FirstSubscription = Hub.Subscribe<FUnitTestEventPayload>(EventName,
        [&Hub, &FirstSeenSum, &SecondSeenSum, &SecondSubscription](const FUnitTestEventPayload& Payload) {
            FirstSeenSum += Payload.Value;
            if (!SecondSubscription.IsValid()) {
                SecondSubscription = Hub.Subscribe<FUnitTestEventPayload>(EventName,
                    [&SecondSeenSum](const FUnitTestEventPayload& InnerPayload) {
                        SecondSeenSum += InnerPayload.Value;
                    });
            }
        });

    Hub.Emit<FUnitTestEventPayload>(EventName, FUnitTestEventPayload{1});
    TestEqual(TEXT("首次广播仅现有订阅者收到"), FirstSeenSum, 1);
    TestEqual(TEXT("回调内新增订阅者未参与当轮遍历"), SecondSeenSum, 0);

    Hub.Emit<FUnitTestEventPayload>(EventName, FUnitTestEventPayload{2});
    TestEqual(TEXT("新增订阅者自下一轮起收到"), SecondSeenSum, 2);
    TestEqual(TEXT("首订阅者累计值正确"), FirstSeenSum, 3);

    FirstSubscription->Reset();
    Hub.Emit<FUnitTestEventPayload>(EventName, FUnitTestEventPayload{4});
    TestEqual(TEXT("退订后首订阅者不再收到"), FirstSeenSum, 3);
    TestEqual(TEXT("退订不影响其余订阅者"), SecondSeenSum, 6);

    SecondSubscription.Reset();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameListenerHubTypeFingerprintTest, "Game.Core.Listener.TypeFingerprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 类型指纹：同名事件不同载荷类型互不串扰
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 *
 * 实现思路：两个独立事件分别以结构体与标量为载荷注册，断言各自广播互不触发对方回调；
 * 同时校验同一类型指纹在不同事件的多次求值稳定一致
 */
bool FGameListenerHubTypeFingerprintTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FListenerHub& Hub = FListenerHub::Get();
    static const FName StructEventName(TEXT("UnitTest.Listener.PayloadStruct"));
    static const FName ScalarEventName(TEXT("UnitTest.Listener.PayloadScalar"));

    int32 StructSeenSum = 0;
    int32 ScalarSeenSum = 0;
    const TSharedRef<FObserverSubscription> StructSubscription = Hub.Subscribe<FUnitTestEventPayload>(StructEventName,
        [&StructSeenSum](const FUnitTestEventPayload& Payload) {
            StructSeenSum += Payload.Value;
        });
    const TSharedRef<FObserverSubscription> ScalarSubscription = Hub.Subscribe<int32>(ScalarEventName,
        [&ScalarSeenSum](const int32& Payload) {
            ScalarSeenSum += Payload;
        });

    Hub.Emit<FUnitTestEventPayload>(StructEventName, FUnitTestEventPayload{3});
    Hub.Emit<int32>(ScalarEventName, 5);
    TestEqual(TEXT("结构体载荷事件命中自身订阅者"), StructSeenSum, 3);
    TestEqual(TEXT("标量载荷事件命中自身订阅者"), ScalarSeenSum, 5);

    Hub.Emit<int32>(StructEventName, 7);
    TestEqual(TEXT("载荷类型不匹配时静默忽略，不误触发"), StructSeenSum, 3);

    Hub.Emit<FUnitTestEventPayload>(FName(TEXT("UnitTest.Listener.NotRegistered")), FUnitTestEventPayload{9});
    TestEqual(TEXT("未注册事件广播静默忽略"), StructSeenSum, 3);

    StructSubscription->Reset();
    ScalarSubscription->Reset();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
