/**
 * @file TCancelableScopeGuardTest.cpp
 * @brief TCancelableScopeGuard 事务作用域守卫单元测试
 */

#include "Types/TCancelableScopeGuard.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCancelableScopeGuardExecutionAndDismissTest,
    "Game.Core.Types.CancelableScopeGuard.ExecutionAndDismiss",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证作用域守卫正常退出时触发回调与主动 Dismiss 拦截回调
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FCancelableScopeGuardExecutionAndDismissTest::RunTest([[maybe_unused]] const FString& Parameters) {
    bool bExecuted = false;
    {
        auto Guard = MakeCancelableScopeGuard([&bExecuted]() {
            bExecuted = true;
        });
        TestTrue(TEXT("构造后守卫默认处于激活状态"), Guard.IsActive());
    }
    TestTrue(TEXT("离开作用域后守卫回调已执行"), bExecuted);

    bool bDismissedExecuted = false;
    {
        auto Guard = MakeCancelableScopeGuard([&bDismissedExecuted]() {
            bDismissedExecuted = true;
        });
        TestTrue(TEXT("构造后激活"), Guard.IsActive());
        Guard.Dismiss();
        TestFalse(TEXT("Dismiss 后守卫处于非激活状态"), Guard.IsActive());
    }
    TestFalse(TEXT("被 Dismiss 的守卫离开作用域绝不执行回调"), bDismissedExecuted);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCancelableScopeGuardMoveSemanticsTest,
    "Game.Core.Types.CancelableScopeGuard.MoveSemantics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证作用域守卫移动构造与移动赋值的所有权转移与析构时序
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FCancelableScopeGuardMoveSemanticsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    int32 ExecuteCount = 0;
    {
        auto Guard1 = MakeCancelableScopeGuard([&ExecuteCount]() {
            ++ExecuteCount;
        });
        TestTrue(TEXT("Guard1 初始激活"), Guard1.IsActive());

        auto Guard2(MoveTemp(Guard1));
        TestFalse(TEXT("移动构造后源守卫已处于非激活状态"), Guard1.IsActive());
        TestTrue(TEXT("移动构造后目标守卫处于激活状态"), Guard2.IsActive());
    }
    TestEqual(TEXT("移动构造后回调仅执行一次"), ExecuteCount, 1);

    int32 FirstExecuted = 0;
    int32 SecondExecuted = 0;
    {
        TFunction<void()> ActionA = [&FirstExecuted]() {
            ++FirstExecuted;
        };
        TFunction<void()> ActionB = [&SecondExecuted]() {
            ++SecondExecuted;
        };

        auto GuardA = MakeCancelableScopeGuard(MoveTemp(ActionA));
        auto GuardB = MakeCancelableScopeGuard(MoveTemp(ActionB));

        TestEqual(TEXT("赋值前 FirstExecuted 为 0"), FirstExecuted, 0);
        GuardA = MoveTemp(GuardB);
        TestEqual(TEXT("移动赋值使原 GuardA 的回调立即被触发结算"), FirstExecuted, 1);
        TestFalse(TEXT("移动赋值后 GuardB 处于非激活状态"), GuardB.IsActive());
        TestTrue(TEXT("移动赋值后 GuardA 接管 GuardB 的激活状态"), GuardA.IsActive());
    }
    TestEqual(TEXT("GuardA 析构时触发接管的 GuardB 回调"), SecondExecuted, 1);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCancelableScopeGuardTransactionalSimulationTest,
    "Game.Core.Types.CancelableScopeGuard.TransactionalSimulation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多步骤事务在失败提前退出时自动回滚，并在全部成功时 Dismiss 提交
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FCancelableScopeGuardTransactionalSimulationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    auto SimulateTransaction = [](bool bShouldFailStepTwo, int32& OutResourceState) -> bool {
        OutResourceState = 1;
        auto RollbackGuard = MakeCancelableScopeGuard([&OutResourceState]() {
            OutResourceState = 0;
        });

        if (bShouldFailStepTwo) {
            return false;
        }

        OutResourceState = 2;
        RollbackGuard.Dismiss();
        return true;
    };

    int32 ResourceStateA = 0;
    const bool bSuccessA = SimulateTransaction(true, ResourceStateA);
    TestFalse(TEXT("第二步失败事务返回 false"), bSuccessA);
    TestEqual(TEXT("失败事务触发自动回滚重置资源状态为 0"), ResourceStateA, 0);

    int32 ResourceStateB = 0;
    const bool bSuccessB = SimulateTransaction(false, ResourceStateB);
    TestTrue(TEXT("全流程成功事务返回 true"), bSuccessB);
    TestEqual(TEXT("成功事务 Dismiss 守卫保留最终资源状态 2"), ResourceStateB, 2);

    return true;
}

#endif // WITH_AUTOMATION_TESTS
