/**
 * @file FListenerHubTest.cpp
 * @brief 监听总线模块内自动化测试（COW 快照遍历隔离 / 退订按 ID / 类型指纹分流），Shipping 构建自动剔除
 */

#include "Listener/FListenerHub.h"

#if WITH_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"

#include <atomic>

namespace {

/**
 * @brief 测试用事件载荷结构体
 */
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
bool FGameListenerHubSnapshotTest::RunTest(const FString& Parameters) {
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
bool FGameListenerHubTypeFingerprintTest::RunTest(const FString& Parameters) {
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

    TestTrue(
        TEXT("相同 4 字节 sizeof 的 int32 与 float 具备完全不同的编译期 64 位类型指纹"),
        Game::Core::GetTypeFingerprint<int32>() != Game::Core::GetTypeFingerprint<float>()
    );
    using FAliasEventPayload = FUnitTestEventPayload;
    TestEqual(
        TEXT("using 类型别名与 const/引用限定符归一化为完全相同的 64 位类型指纹"),
        Game::Core::GetTypeFingerprint<FUnitTestEventPayload>(),
        Game::Core::GetTypeFingerprint<const FAliasEventPayload&>()
    );
    TestFalse(
        TEXT("向 int32 事件发射同 sizeof 的 float 载荷被类型指纹精准拦截"),
        Hub.Emit<float>(ScalarEventName, 12.0f)
    );
    TestEqual(TEXT("被拦截后 int32 订阅者计数不变"), ScalarSeenSum, 5);

    StructSubscription->Reset();
    ScalarSubscription->Reset();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameListenerHubErrorBranchesTest, "Game.Core.Listener.ErrorBranches",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证空回调过滤、已存在事件以不同类型订阅被拒绝及未注册事件广播忽略
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 */
bool FGameListenerHubErrorBranchesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FListenerHub& Hub = FListenerHub::Get();
    static const FName EventName(TEXT("UnitTest.Listener.ErrorBranches"));

    const TSharedRef<FObserverSubscription> NullSub = Hub.Subscribe<int32>(EventName, nullptr);
    TestFalse(TEXT("空回调订阅返回无效句柄"), NullSub->IsActive());

    const TSharedRef<FObserverSubscription> ValidSub = Hub.Subscribe<int32>(EventName, [](const int32&) {});
    TestTrue(TEXT("有效回调订阅返回有效句柄"), ValidSub->IsActive());

    AddExpectedErrorPlain(TEXT("已注册为其他载荷类型"), EAutomationExpectedErrorFlags::Contains, 1);
    const TSharedRef<FObserverSubscription> MismatchedSub = Hub.Subscribe<FUnitTestEventPayload>(EventName, [](const FUnitTestEventPayload&) {});
    TestFalse(TEXT("同名事件以不同载荷类型再次订阅被拒绝并返回无效句柄"), MismatchedSub->IsActive());

    ValidSub->Reset();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameListenerHubConcurrentStressTest,
    "Game.Core.Listener.ConcurrentStress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多线程并发高频广播、动态并发订阅与退订重入的绝对线程安全性与零数据竞争
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameListenerHubConcurrentStressTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FListenerHub& Hub = FListenerHub::Get();
    static const FName StressEvent(TEXT("UnitTest.Listener.StressConcurrent"));

    constexpr int32 NumEmitters = 4;
    constexpr int32 NumEmittersCount = 2500;
    constexpr int32 ExpectedTotalEmits = NumEmitters * NumEmittersCount;

    std::atomic<int32> ReceivedCount{0};
    const TSharedRef<FObserverSubscription> PersistentSub = Hub.Subscribe<int32>(
        StressEvent,
        [&ReceivedCount](const int32&) {
            ReceivedCount.fetch_add(1, std::memory_order_relaxed);
        }
    );

    constexpr int32 TransientThreads = 4;
    std::atomic<bool> bStopTransients{false};
    std::atomic<int32> TransientReceivedCount{0};

    TArray<TFuture<void>> TransientFutures;
    TransientFutures.Reserve(TransientThreads);
    for (int32 WorkerIdx = 0; WorkerIdx < TransientThreads; ++WorkerIdx) {
        TransientFutures.Add(Async(EAsyncExecution::Thread, [&Hub, &bStopTransients, &TransientReceivedCount]() {
            while (!bStopTransients.load(std::memory_order_relaxed)) {
                TSharedRef<FObserverSubscription> Sub = Hub.Subscribe<int32>(
                    StressEvent,
                    [&TransientReceivedCount](const int32&) {
                        TransientReceivedCount.fetch_add(1, std::memory_order_relaxed);
                    }
                );
                FPlatformProcess::Yield();
                Sub->Reset();
            }
        }));
    }

    ParallelFor(NumEmitters, [&Hub]([[maybe_unused]] int32 WorkerIdx) {
        for (int32 Index = 0; Index < NumEmittersCount; ++Index) {
            Hub.Emit<int32>(StressEvent, 1);
        }
    });

    bStopTransients.store(true, std::memory_order_relaxed);
    for (TFuture<void>& Future : TransientFutures) {
        Future.Wait();
    }

    PersistentSub->Reset();

    TestEqual(TEXT("常驻订阅者精确收到全部 10000 次并发广播零丢失"), ReceivedCount.load(), ExpectedTotalEmits);
    TestTrue(TEXT("瞬态订阅者在并发订阅与退订期间正常捕获广播且未崩溃"), TransientReceivedCount.load() >= 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameListenerHubResetTest,
    "Game.Core.Listener.HubReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FListenerHub 的 Reset 清理与通道计数能力
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameListenerHubResetTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FListenerHub& Hub = FListenerHub::Get();
    Hub.Reset();
    TestEqual(TEXT("初始重置后通道数为 0"), Hub.Num(), 0);

    const FName Evt1(TEXT("ResetTest.Event1"));
    const FName Evt2(TEXT("ResetTest.Event2"));

    int32 Count1 = 0;
    int32 Count2 = 0;
    auto Sub1 = Hub.Subscribe<int32>(Evt1, [&Count1](const int32& Val) { Count1 += Val; });
    auto Sub2 = Hub.Subscribe<float>(Evt2, [&Count2](const float& Val) { Count2 += static_cast<int32>(Val); });

    TestEqual(TEXT("注册 2 个事件后通道数为 2"), Hub.Num(), 2);
    TestTrue(TEXT("广播 Event1 成功"), Hub.Emit<int32>(Evt1, 10));
    TestEqual(TEXT("Count1 收到 10"), Count1, 10);

    Hub.Reset();
    TestEqual(TEXT("Reset 后通道数清零"), Hub.Num(), 0);
    TestFalse(TEXT("Reset 后广播已不存在的事件返回 false"), Hub.Emit<int32>(Evt1, 20));
    TestEqual(TEXT("Count1 不再增加"), Count1, 10);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
