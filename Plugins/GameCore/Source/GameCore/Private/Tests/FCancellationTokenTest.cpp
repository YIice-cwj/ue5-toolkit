/**
 * @file FCancellationTokenTest.cpp
 * @brief 异步取消令牌与取消源自动化单元测试
 */

#include "Async/FCancellationToken.h"
#include "Async/FCancellationTokenSource.h"

#if WITH_AUTOMATION_TESTS

#include <atomic>

#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCancellationTokenBasicLifecycleTest,
    "Game.Core.Async.CancellationToken.BasicLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证取消令牌与取消源的基本状态轮询、取消触发与重复调用幂等性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FCancellationTokenBasicLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FCancellationToken NoneToken = FCancellationToken::None();
    TestFalse(TEXT("None 令牌不可被取消"), NoneToken.CanBeCancelled());
    TestFalse(TEXT("None 令牌初始未被取消"), NoneToken.IsCancellationRequested());

    FCancellationTokenSource Source;
    const FCancellationToken Token = Source.GetToken();

    TestTrue(TEXT("Source 派生的令牌可被取消"), Token.CanBeCancelled());
    TestFalse(TEXT("初始状态未被请求取消"), Source.IsCancellationRequested());
    TestFalse(TEXT("初始状态令牌未被请求取消"), Token.IsCancellationRequested());
    TestFalse(TEXT("初始状态令牌 explicit operator bool 返回 false"), static_cast<bool>(Token));

    const FCancellationToken TokenCopy = Token;
    TestTrue(TEXT("相同源派生的令牌 operator== 成立"), TokenCopy == Token);
    TestFalse(TEXT("与 None 令牌 operator== 返回 false"), Token == NoneToken);
    TestTrue(TEXT("与 None 令牌 operator!= 返回 true"), Token != NoneToken);

    TestTrue(TEXT("首次 Cancel 返回 true"), Source.Cancel());
    TestTrue(TEXT("取消后 Source 状态处于已取消"), Source.IsCancellationRequested());
    TestTrue(TEXT("取消后 Token 状态处于已取消"), Token.IsCancellationRequested());
    TestTrue(TEXT("取消后 Token explicit operator bool 返回 true"), static_cast<bool>(Token));

    TestFalse(TEXT("重复调用 Cancel 返回 false 幂等防重入"), Source.Cancel());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCancellationTokenCallbackDispatchTest,
    "Game.Core.Async.CancellationToken.CallbackDispatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证取消回调注册、出锁派发、提前注销解绑以及已取消态立即执行机制
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FCancellationTokenCallbackDispatchTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FCancellationTokenSource Source;
    const FCancellationToken Token = Source.GetToken();

    int32 CallbackACallCount = 0;
    int32 CallbackBCallCount = 0;

    FCancellationTokenRegistration RegA = Token.Register([&CallbackACallCount]() {
        ++CallbackACallCount;
    });
    TestTrue(TEXT("RegA 处于注册状态"), RegA.IsRegistered());

    {
        FCancellationTokenRegistration RegB = Token.Register([&CallbackBCallCount]() {
            ++CallbackBCallCount;
        });
        TestTrue(TEXT("RegB 注册后处于有效状态"), RegB.IsRegistered());
        RegB.Unregister();
        TestFalse(TEXT("RegB Unregister 后不再处于注册状态"), RegB.IsRegistered());
    }

    TestEqual(TEXT("触发前 CallbackA 未执行"), CallbackACallCount, 0);
    TestEqual(TEXT("触发前 CallbackB 未执行"), CallbackBCallCount, 0);

    Source.Cancel();

    TestEqual(TEXT("Cancel 后 CallbackA 被执行 1 次"), CallbackACallCount, 1);
    TestEqual(TEXT("主动 Unregister 的 CallbackB 未被执行"), CallbackBCallCount, 0);

    int32 LateCallbackCallCount = 0;
    FCancellationTokenRegistration RegLate = Token.Register([&LateCallbackCallCount]() {
        ++LateCallbackCallCount;
    });
    TestEqual(TEXT("已取消状态下注册的回调立即同步执行"), LateCallbackCallCount, 1);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCancellationTokenLinkedSourceTest,
    "Game.Core.Async.CancellationToken.LinkedSource",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证级联联合取消源在任一输入令牌触发时的联动取消传播
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FCancellationTokenLinkedSourceTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FCancellationTokenSource SourceA;
    FCancellationTokenSource SourceB;

    const TSharedRef<FCancellationTokenSource> LinkedSource = FCancellationTokenSource::CreateLinkedTokenSource(
        SourceA.GetToken(),
        SourceB.GetToken()
    );

    const FCancellationToken LinkedToken = LinkedSource->GetToken();
    TestFalse(TEXT("联合令牌初始处于未取消态"), LinkedToken.IsCancellationRequested());

    SourceB.Cancel();
    TestTrue(TEXT("SourceB 取消后级联导致 LinkedToken 处于已取消态"), LinkedToken.IsCancellationRequested());
    TestTrue(TEXT("LinkedSource 本身也处于已取消态"), LinkedSource->IsCancellationRequested());

    FCancellationTokenSource Source1;
    FCancellationTokenSource Source2;
    FCancellationTokenSource Source3;
    const FCancellationToken MultiTokens[] = { Source1.GetToken(), Source2.GetToken(), Source3.GetToken() };
    const TSharedRef<FCancellationTokenSource> MultiLinked = FCancellationTokenSource::CreateLinkedTokenSource(
        MakeArrayView(MultiTokens)
    );
    TestFalse(TEXT("三元联合令牌初始处于未取消态"), MultiLinked->IsCancellationRequested());

    Source3.Cancel();
    TestTrue(TEXT("Source3 取消后三元联合令牌处于已取消态"), MultiLinked->IsCancellationRequested());

    const FCancellationToken PreCancelledTokens[] = { Source1.GetToken(), Source3.GetToken() };
    const TSharedRef<FCancellationTokenSource> PreCancelledLinked = FCancellationTokenSource::CreateLinkedTokenSource(
        MakeArrayView(PreCancelledTokens)
    );
    TestTrue(TEXT("包含已取消令牌时新创建的联合令牌立即处于已取消态"), PreCancelledLinked->IsCancellationRequested());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCancellationTokenConcurrentStressTest,
    "Game.Core.Async.CancellationToken.ConcurrentStress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 真实多线程并发：工作线程高频并发注册、轮询、局部退订与出锁取消对撞压力测试（零 Sleep 盲等）
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FCancellationTokenConcurrentStressTest::RunTest([[maybe_unused]] const FString& Parameters) {
    constexpr int32 NumThreads = 8;
    constexpr int32 CallbacksPerThread = 200;
    constexpr int32 TotalDirectCallbacks = NumThreads * CallbacksPerThread;

    FCancellationTokenSource SourceA;
    const FCancellationToken TokenA = SourceA.GetToken();

    std::atomic<int32> ExecutedCallbackCountA{0};
    std::atomic<bool> bStartSignalA{false};

    TArray<TFuture<void>> WorkerFuturesA;
    WorkerFuturesA.Reserve(NumThreads);

    for (int32 ThreadIdx = 0; ThreadIdx < NumThreads; ++ThreadIdx) {
        WorkerFuturesA.Add(Async(EAsyncExecution::Thread, [TokenA, &ExecutedCallbackCountA, &bStartSignalA, CallbacksPerThread]() {
            while (!bStartSignalA.load(std::memory_order_acquire)) {
                FPlatformProcess::Yield();
            }

            TArray<FCancellationTokenRegistration> RetainedRegistrations;
            RetainedRegistrations.Reserve(CallbacksPerThread);

            for (int32 Step = 0; Step < CallbacksPerThread; ++Step) {
                RetainedRegistrations.Add(TokenA.Register([&ExecutedCallbackCountA]() {
                    ExecutedCallbackCountA.fetch_add(1, std::memory_order_relaxed);
                }));
            }

            while (!TokenA.IsCancellationRequested()) {
                FPlatformProcess::Yield();
            }
        }));
    }

    TFuture<void> CancellerFuture = Async(EAsyncExecution::Thread, [&SourceA, &bStartSignalA]() {
        while (!bStartSignalA.load(std::memory_order_acquire)) {
            FPlatformProcess::Yield();
        }
        FPlatformProcess::Yield();
        SourceA.Cancel();
    });

    bStartSignalA.store(true, std::memory_order_release);

    for (TFuture<void>& Future : WorkerFuturesA) {
        Future.Wait();
    }
    CancellerFuture.Wait();

    TestTrue(TEXT("取消后 TokenA 处于已取消态"), TokenA.IsCancellationRequested());
    TestEqual(TEXT("高并发注册与取消对撞下所有 1600 个回调精确触发且零丢失零重复"), ExecutedCallbackCountA.load(), TotalDirectCallbacks);

    FCancellationTokenSource SourceB;
    const FCancellationToken TokenB = SourceB.GetToken();
    std::atomic<int32> ExecutedCallbackCountB{0};
    std::atomic<bool> bStartSignalB{false};

    TArray<TFuture<void>> WorkerFuturesB;
    WorkerFuturesB.Reserve(NumThreads);

    for (int32 ThreadIdx = 0; ThreadIdx < NumThreads; ++ThreadIdx) {
        WorkerFuturesB.Add(Async(EAsyncExecution::Thread, [TokenB, &ExecutedCallbackCountB, &bStartSignalB, CallbacksPerThread]() {
            while (!bStartSignalB.load(std::memory_order_acquire)) {
                FPlatformProcess::Yield();
            }

            TArray<FCancellationTokenRegistration> LocalRegistrations;
            LocalRegistrations.Reserve(CallbacksPerThread);

            for (int32 Step = 0; Step < CallbacksPerThread; ++Step) {
                LocalRegistrations.Add(TokenB.Register([&ExecutedCallbackCountB]() {
                    ExecutedCallbackCountB.fetch_add(1, std::memory_order_relaxed);
                }));
            }

            for (FCancellationTokenRegistration& Reg : LocalRegistrations) {
                Reg.Unregister();
            }
        }));
    }

    bStartSignalB.store(true, std::memory_order_release);
    for (TFuture<void>& Future : WorkerFuturesB) {
        Future.Wait();
    }

    SourceB.Cancel();
    TestEqual(TEXT("全部退订的回调在 Cancel 后触发计数严格为 0"), ExecutedCallbackCountB.load(), 0);

    return true;
}

#endif
