/**
 * @file TPriorityArbitratorTest.cpp
 * @brief TPriorityArbitrator 多源优先级属性仲裁器自动化单元测试
 */

#include "State/TPriorityArbitrator.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPriorityArbitratorOverrideModeTest,
    "Game.Core.State.PriorityArbitrator.OverrideMode",
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
    TestFalse(TEXT("未注册通道 GetChannelValue 返回 unset"), Arbitrator.GetChannelValue(FName(TEXT("NonExistent"))).IsSet());

    Arbitrator.SetChannel(FName(TEXT("BaseMovement")), 500.0f, 10);
    TestTrue(TEXT("已注册通道 GetChannelValue 返回 set"), Arbitrator.GetChannelValue(FName(TEXT("BaseMovement"))).IsSet());
    TestEqual(TEXT("可选对象数值 500.0"), Arbitrator.GetChannelValue(FName(TEXT("BaseMovement"))).GetValue(), 500.0f);
    TestTrue(TEXT("已注册通道 GetChannelPriority 返回 set"), Arbitrator.GetChannelPriority(FName(TEXT("BaseMovement"))).IsSet());
    TestEqual(TEXT("已注册通道优先级为 10"), Arbitrator.GetChannelPriority(FName(TEXT("BaseMovement"))).GetValue(), 10);
    TestFalse(TEXT("未注册通道 GetChannelPriority 返回 unset"), Arbitrator.GetChannelPriority(FName(TEXT("NonExistent"))).IsSet());
    TestEqual(TEXT("单一通道生效，当前值 500.0"), Arbitrator.GetEffectiveValue(1.0f), 500.0f);
    TestEqual(TEXT("胜出通道为 BaseMovement"), Arbitrator.GetWinningChannel(), FName(TEXT("BaseMovement")));

    Arbitrator.SetChannel(FName(TEXT("SprintBuff")), 800.0f, 20);
    TestEqual(TEXT("更高优先级 SprintBuff 抢占，当前值 800.0"), Arbitrator.GetEffectiveValue(1.0f), 800.0f);
    TestEqual(TEXT("胜出通道为 SprintBuff"), Arbitrator.GetWinningChannel(), FName(TEXT("SprintBuff")));

    Arbitrator.SetChannel(FName(TEXT("SlowDebuff")), 200.0f, 20);
    TestEqual(TEXT("相同优先级最新更新通道 SlowDebuff 胜出"), Arbitrator.GetEffectiveValue(1.0f), 200.0f);
    TestEqual(TEXT("胜出通道为 SlowDebuff"), Arbitrator.GetWinningChannel(), FName(TEXT("SlowDebuff")));

    TArray<FName> ChannelNames;
    Arbitrator.GetChannelNames(ChannelNames);
    TestEqual(TEXT("注册 3 个通道后 GetChannelNames 数量为 3"), ChannelNames.Num(), 3);
    TestTrue(TEXT("通道名称列表包含 BaseMovement"), ChannelNames.Contains(FName(TEXT("BaseMovement"))));
    TestTrue(TEXT("通道名称列表包含 SprintBuff"), ChannelNames.Contains(FName(TEXT("SprintBuff"))));
    TestTrue(TEXT("通道名称列表包含 SlowDebuff"), ChannelNames.Contains(FName(TEXT("SlowDebuff"))));

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
    "Game.Core.State.PriorityArbitrator.MathModes",
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

    const float AdditiveCachedResult = Arbitrator.GetEffectiveValue(100.0f, EPriorityArbitrationMode::Additive);
    TestEqual(TEXT("加算模式缓存命中：保持 135.0"), AdditiveCachedResult, 135.0f);

    Arbitrator.SetChannel(FName(TEXT("ModC")), 5.0f);
    const float AdditiveAfterUpdate = Arbitrator.GetEffectiveValue(100.0f, EPriorityArbitrationMode::Additive);
    TestEqual(TEXT("更新通道后置脏重算：100 + 10 + 25 + 5 = 140.0"), AdditiveAfterUpdate, 140.0f);

    Arbitrator.Reset();
    Arbitrator.SetChannel(FName(TEXT("TimeScaleSkill")), 0.5f);
    Arbitrator.SetChannel(FName(TEXT("TimeScaleGlobal")), 0.2f);

    const float MultiplicativeResult = Arbitrator.GetEffectiveValue(1.0f, EPriorityArbitrationMode::Multiplicative);
    TestEqual(TEXT("乘算模式：1.0 * 0.5 * 0.2 = 0.1"), MultiplicativeResult, 0.1f);

    const float MultiplicativeCached = Arbitrator.GetEffectiveValue(1.0f, EPriorityArbitrationMode::Multiplicative);
    TestEqual(TEXT("乘算模式缓存命中：保持 0.1"), MultiplicativeCached, 0.1f);

    TPriorityArbitrator<float> ConfiguredAdditive(EPriorityArbitrationMode::Additive);
    TestEqual(TEXT("构造器初始化仲裁模式为加算"), ConfiguredAdditive.GetMode(), EPriorityArbitrationMode::Additive);
    ConfiguredAdditive.SetChannel(FName(TEXT("AtkBuff")), 50.0f);
    TestEqual(TEXT("单实参 GetEffectiveValue 依自身配置模式返回 150.0"), ConfiguredAdditive.GetEffectiveValue(100.0f), 150.0f);

    ConfiguredAdditive.SetMode(EPriorityArbitrationMode::Multiplicative);
    TestEqual(TEXT("SetMode 动态切为乘算模式"), ConfiguredAdditive.GetMode(), EPriorityArbitrationMode::Multiplicative);
    TestEqual(TEXT("乘算单实参计算 100 * 50 = 5000.0"), ConfiguredAdditive.GetEffectiveValue(100.0f), 5000.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPriorityArbitratorMoveSemanticsTest,
    "Game.Core.State.PriorityArbitrator.MoveSemantics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证移动构造与移动赋值后的缓存重置、无悬垂引用与运算正确性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FPriorityArbitratorMoveSemanticsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TPriorityArbitrator<float> Source(EPriorityArbitrationMode::Additive);
    Source.SetChannel(FName(TEXT("Buff1")), 10.0f);
    Source.SetChannel(FName(TEXT("Buff2")), 20.0f);

    const float SourceResult = Source.GetEffectiveValue(100.0f);
    TestEqual(TEXT("原仲裁器计算结果为 130.0"), SourceResult, 130.0f);

    TPriorityArbitrator<float> Dest = MoveTemp(Source);
    TestEqual(TEXT("移动后新对象计算结果依然为 130.0"), Dest.GetEffectiveValue(100.0f), 130.0f);
    TestTrue(TEXT("新对象包含通道 Buff1"), Dest.HasChannel(FName(TEXT("Buff1"))));
    TestTrue(TEXT("新对象包含通道 Buff2"), Dest.HasChannel(FName(TEXT("Buff2"))));

    Dest.SetChannel(FName(TEXT("Buff3")), 5.0f);
    TestEqual(TEXT("新对象继续加算计算结果为 135.0"), Dest.GetEffectiveValue(100.0f), 135.0f);

    TPriorityArbitrator<float> AssignedDest(EPriorityArbitrationMode::Multiplicative);
    AssignedDest = MoveTemp(Dest);
    TestEqual(TEXT("移动赋值后模式继承为加算"), AssignedDest.GetMode(), EPriorityArbitrationMode::Additive);
    TestEqual(TEXT("移动赋值后运算结果依然为 135.0"), AssignedDest.GetEffectiveValue(100.0f), 135.0f);

    return true;
}

#endif
