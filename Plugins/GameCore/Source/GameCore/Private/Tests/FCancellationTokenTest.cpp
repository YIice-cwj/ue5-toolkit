/**
 * @file FCancellationTokenTest.cpp
 * @brief 异步取消令牌与取消源自动化单元测试
 */

#include "Types/FCancellationToken.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCancellationTokenBasicLifecycleTest,
    "Game.Core.Types.CancellationToken.BasicLifecycle",
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

    TestTrue(TEXT("首次 Cancel 返回 true"), Source.Cancel());
    TestTrue(TEXT("取消后 Source 状态处于已取消"), Source.IsCancellationRequested());
    TestTrue(TEXT("取消后 Token 状态处于已取消"), Token.IsCancellationRequested());

    TestFalse(TEXT("重复调用 Cancel 返回 false 幂等防重入"), Source.Cancel());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCancellationTokenCallbackDispatchTest,
    "Game.Core.Types.CancellationToken.CallbackDispatch",
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

    {
        FCancellationTokenRegistration RegB = Token.Register([&CallbackBCallCount]() {
            ++CallbackBCallCount;
        });
        RegB.Unregister();
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
    "Game.Core.Types.CancellationToken.LinkedSource",
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

    return true;
}

#endif
