/**
 * @file FFixedTimestepAccumulatorTest.cpp
 * @brief FFixedTimestepAccumulator 单元测试（步长离散计算、防死锁截断与插值 Alpha 精度）
 */

#include "Determinism/FFixedTimestepAccumulator.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFixedTimestepAccumulatorStepAndAlphaTest,
    "Game.Core.Determinism.FixedTimestep.StepAndAlpha",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证固定步长推进、离散累加与渲染插值因子 Alpha 计算
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FFixedTimestepAccumulatorStepAndAlphaTest::RunTest([[maybe_unused]] const FString& Parameters) {
    constexpr double FixedDt = 0.02;
    FFixedTimestepAccumulator Accumulator(FixedDt, 4, 0.25);

    TestEqual(TEXT("初始固定步长正确"), Accumulator.GetFixedDeltaTime(), FixedDt);
    TestEqual(TEXT("初始累计时间为 0"), Accumulator.GetAccumulatedTime(), 0.0);
    TestEqual(TEXT("初始插值 Alpha 为 0"), Accumulator.GetInterpolationAlpha(), 0.0);

    TestEqual(TEXT("输入 0 秒流逝执行 0 次子步"), Accumulator.Advance(0.0, [](double) {}), 0);
    TestEqual(TEXT("输入负流逝时间安全返回 0"), Accumulator.Advance(-0.01, [](double) {}), 0);

    int32 ExecutedSteps = 0;
    double ReceivedDt = 0.0;
    const int32 Steps1 = Accumulator.Advance(0.01, [&ExecutedSteps, &ReceivedDt](double Dt) {
        ++ExecutedSteps;
        ReceivedDt = Dt;
    });
    TestEqual(TEXT("流逝未达固定步长执行 0 步"), Steps1, 0);
    TestEqual(TEXT("未触发回调"), ExecutedSteps, 0);
    TestNearlyEqual(TEXT("已累计 0.01 秒"), Accumulator.GetAccumulatedTime(), 0.01, 0.0001);
    TestNearlyEqual(TEXT("插值 Alpha 为 0.5"), Accumulator.GetInterpolationAlpha(), 0.5, 0.0001);

    const int32 Steps2 = Accumulator.Advance(0.015, [&ExecutedSteps, &ReceivedDt](double Dt) {
        ++ExecutedSteps;
        ReceivedDt = Dt;
    });
    TestEqual(TEXT("再次累加后凑齐 1 步"), Steps2, 1);
    TestEqual(TEXT("回调执行 1 次"), ExecutedSteps, 1);
    TestEqual(TEXT("子步回调接收固定步长"), ReceivedDt, FixedDt);
    TestNearlyEqual(TEXT("消费后余量为 0.005 秒"), Accumulator.GetAccumulatedTime(), 0.005, 0.0001);
    TestNearlyEqual(TEXT("插值 Alpha 为 0.25"), Accumulator.GetInterpolationAlpha(), 0.25, 0.0001);

    Accumulator.Reset();
    TestEqual(TEXT("Reset 后累计时间归零"), Accumulator.GetAccumulatedTime(), 0.0);
    TestEqual(TEXT("Reset 后插值 Alpha 归零"), Accumulator.GetInterpolationAlpha(), 0.0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFixedTimestepAccumulatorSpiralOfDeathProtectionTest,
    "Game.Core.Determinism.FixedTimestep.SpiralOfDeathProtection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证突发掉帧与极端延迟时的 MaxSubsteps 截断与防螺旋死锁丢帧保护
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FFixedTimestepAccumulatorSpiralOfDeathProtectionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    constexpr double FixedDt = 0.01;
    constexpr int32 MaxSteps = 3;
    constexpr double MaxDt = 0.1;
    FFixedTimestepAccumulator Accumulator(FixedDt, MaxSteps, MaxDt);

    int32 TotalExecuted = 0;
    const int32 Steps = Accumulator.Advance(0.08, [&TotalExecuted](double) {
        ++TotalExecuted;
    });

    TestEqual(TEXT("80ms 包含 8 个步长但被截断为最大 3 步"), Steps, MaxSteps);
    TestEqual(TEXT("回调实际执行恰好 3 次"), TotalExecuted, MaxSteps);
    TestTrue(TEXT("过量余量被清理且未死锁积压"), Accumulator.GetAccumulatedTime() < FixedDt);

    Accumulator.SetMaxSubsteps(5);
    TestEqual(TEXT("动态更新最大子步数成功"), Accumulator.GetMaxSubsteps(), 5);

    Accumulator.SetFixedDeltaTime(0.05);
    TestEqual(TEXT("动态更新固定步长成功"), Accumulator.GetFixedDeltaTime(), 0.05);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
