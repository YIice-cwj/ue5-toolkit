/**
 * @file FBootstrapRetryGateTest.cpp
 * @brief GAS 引导限次重试闸门自动化测试套件
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Ability/FBootstrapRetryGate.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBootstrapRetryGateTest,
    "Game.Gameplay.Bootstrap.RetryGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证限次重试闸门累计、超限判定、重置与零上限边界条件
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FBootstrapRetryGateTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FBootstrapRetryGate Gate;
    Gate.Configure(0.05f, 3);

    TestEqual(TEXT("初始尝试次数为零"), Gate.GetAttempt(), 0);
    TestEqual(TEXT("次数上限配置生效"), Gate.GetMaxRetries(), 3);
    TestNearlyEqual(TEXT("重试间隔配置生效"), Gate.GetIntervalSeconds(), 0.05f, 0.0001f);

    TestTrue(TEXT("第一次尝试允许"), Gate.Advance());
    TestTrue(TEXT("第二次尝试允许"), Gate.Advance());
    TestTrue(TEXT("第三次尝试允许"), Gate.Advance());
    TestEqual(TEXT("尝试次数累计为三"), Gate.GetAttempt(), 3);
    TestFalse(TEXT("第四次尝试超限"), Gate.Advance());
    TestEqual(TEXT("超限后次数仍累计"), Gate.GetAttempt(), 4);

    Gate.Reset();
    TestEqual(TEXT("重置后次数归零"), Gate.GetAttempt(), 0);
    TestEqual(TEXT("重置不改变上限配置"), Gate.GetMaxRetries(), 3);
    TestTrue(TEXT("重置后重新允许尝试"), Gate.Advance());

    FBootstrapRetryGate ZeroGate;
    ZeroGate.Configure(0.05f, 0);
    TestFalse(TEXT("上限为零时首次尝试即拒绝"), ZeroGate.Advance());

    return true;
}

#endif
