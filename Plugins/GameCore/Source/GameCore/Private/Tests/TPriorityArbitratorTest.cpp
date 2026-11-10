/**
 * @file TPriorityArbitratorTest.cpp
 * @brief TPriorityArbitrator 多源优先级属性仲裁器自动化单元测试
 */

#include "State/TPriorityArbitrator.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPriorityArbitratorOverrideModeTest,
    "Game.Core.Arbitration.PriorityArbitrator.OverrideMode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证属性仲裁器在最高优先级抢占模式下的通道覆盖、平级时序仲裁与通道清理回退
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FPriorityArbitratorOverrideModeTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TPriorityArbitrator<float> Arbitrator;

    TestTrue(TEXT("初始状态为空"), Arbitrator.IsEmpty());
    TestEqual(TEXT("初始状态返回默认基准值 1.0"), Arbitrator.GetEffectiveValue(1.0f), 1.0f);
    TestEqual(TEXT("初始胜出通道为 NAME_None"), Arbitrator.GetWinningChannel(), NAME_None);
    TestNull(TEXT("未注册通道 FindChannelValue 返回 nullptr"), Arbitrator.FindChannelValue(FName(TEXT("NonExistent"))));
    TestFalse(TEXT("未注册通道 GetChannelValue 返回 unset"), Arbitrator.GetChannelValue(FName(TEXT("NonExistent"))).IsSet());

    Arbitrator.SetChannel(FName(TEXT("BaseMovement")), 500.0f, 10);
    TestNotNull(TEXT("已注册通道 FindChannelValue 返回有效指针"), Arbitrator.FindChannelValue(FName(TEXT("BaseMovement"))));
    TestEqual(TEXT("指针指向数值 500.0"), *Arbitrator.FindChannelValue(FName(TEXT("BaseMovement"))), 500.0f);
    TestTrue(TEXT("已注册通道 GetChannelValue 返回 set"), Arbitrator.GetChannelValue(FName(TEXT("BaseMovement"))).IsSet());
    TestEqual(TEXT("可选对象数值 500.0"), Arbitrator.GetChannelValue(FName(TEXT("BaseMovement"))).GetValue(), 500.0f);
    TestEqual(TEXT("单一通道生效，当前值 500.0"), Arbitrator.GetEffectiveValue(1.0f), 500.0f);
    TestEqual(TEXT("胜出通道为 BaseMovement"), Arbitrator.GetWinningChannel(), FName(TEXT("BaseMovement")));

    Arbitrator.SetChannel(FName(TEXT("SprintBuff")), 800.0f, 20);
    TestEqual(TEXT("更高优先级 SprintBuff 抢占，当前值 800.0"), Arbitrator.GetEffectiveValue(1.0f), 800.0f);
    TestEqual(TEXT("胜出通道为 SprintBuff"), Arbitrator.GetWinningChannel(), FName(TEXT("SprintBuff")));

    Arbitrator.SetChannel(FName(TEXT("SlowDebuff")), 200.0f, 20);
    TestEqual(TEXT("相同优先级最新更新通道 SlowDebuff 胜出"), Arbitrator.GetEffectiveValue(1.0f), 200.0f);
    TestEqual(TEXT("胜出通道为 SlowDebuff"), Arbitrator.GetWinningChannel(), FName(TEXT("SlowDebuff")));

    TestTrue(TEXT("清除 SlowDebuff 通道成功"), Arbitrator.ClearChannel(FName(TEXT("SlowDebuff"))));
    TestEqual(TEXT("清除后回退至次早同级通道 SprintBuff (800.0)"), Arbitrator.GetEffectiveValue(1.0f), 800.0f);

    TestTrue(TEXT("清除 SprintBuff 成功"), Arbitrator.ClearChannel(FName(TEXT("SprintBuff"))));
    TestEqual(TEXT("清除后回退至低优先级 BaseMovement (500.0)"), Arbitrator.GetEffectiveValue(1.0f), 500.0f);

    Arbitrator.Reset();
    TestTrue(TEXT("Reset 后恢复为空"), Arbitrator.IsEmpty());
    TestEqual(TEXT("Reset 后回退至默认值"), Arbitrator.GetEffectiveValue(1.0f), 1.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPriorityArbitratorMathModesTest,
    "Game.Core.Arbitration.PriorityArbitrator.MathModes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证属性仲裁器在加算累加与乘算累乘模式下的数学计算正确性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FPriorityArbitratorMathModesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TPriorityArbitrator<float> Arbitrator;

    Arbitrator.SetChannel(FName(TEXT("ModA")), 10.0f);
    Arbitrator.SetChannel(FName(TEXT("ModB")), 25.0f);

    const float AdditiveResult = Arbitrator.GetEffectiveValue(100.0f, EPriorityArbitrationMode::Additive);
    TestEqual(TEXT("加算模式：100 + 10 + 25 = 135.0"), AdditiveResult, 135.0f);

    Arbitrator.Reset();
    Arbitrator.SetChannel(FName(TEXT("TimeScaleSkill")), 0.5f);
    Arbitrator.SetChannel(FName(TEXT("TimeScaleGlobal")), 0.2f);

    const float MultiplicativeResult = Arbitrator.GetEffectiveValue(1.0f, EPriorityArbitrationMode::Multiplicative);
    TestEqual(TEXT("乘算模式：1.0 * 0.5 * 0.2 = 0.1"), MultiplicativeResult, 0.1f);

    return true;
}

#endif
