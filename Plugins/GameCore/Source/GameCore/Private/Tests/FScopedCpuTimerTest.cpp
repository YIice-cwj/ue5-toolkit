/**
 * @file FScopedCpuTimerTest.cpp
 * @brief RAII CPU 微秒计时器与滑动平均采样器单元测试
 */

#include "Profiling/FScopedCpuTimer.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FScopedCpuTimerBasicTest,
    "Game.Core.Profiling.CpuTimer.Basic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FScopedCpuTimer 耗时统计、作用域析构回调与重置机制
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FScopedCpuTimerBasicTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedCpuTimer Timer;
    TestTrue(TEXT("起始周期计数大于 0"), Timer.GetStartCycles() > 0);
    const double ElapsedSec = Timer.GetElapsedSeconds();
    const double ElapsedMs = Timer.GetElapsedMilliseconds();
    const double ElapsedUs = Timer.GetElapsedMicroseconds();

    TestTrue(TEXT("流逝秒数非负"), ElapsedSec >= 0.0);
    TestTrue(TEXT("流逝毫秒非负"), ElapsedMs >= 0.0);
    TestTrue(TEXT("流逝微秒非负"), ElapsedUs >= 0.0);

    Timer.Reset();
    TestTrue(TEXT("重置后流逝时间有效"), Timer.GetElapsedSeconds() >= 0.0);

    double CapturedUs = -1.0;
    {
        GAME_SCOPED_CPU_TIMER(ScopeTimer, [&CapturedUs](double Us) {
            CapturedUs = Us;
        });
        volatile int32 Sum = 0;
        for (int32 Index = 0; Index < 1000; ++Index) {
            Sum += Index;
        }
    }

    TestTrue(TEXT("析构回调捕获到微秒耗时"), CapturedUs >= 0.0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMovingAverageSamplerTest,
    "Game.Core.Profiling.CpuTimer.MovingAverageSampler",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证滑动平均采样器的窗口滑动、极值与平均值计算
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FMovingAverageSamplerTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FMovingAverageSampler<4> Sampler;

    TestTrue(TEXT("初始状态 IsEmpty 为真"), Sampler.IsEmpty());
    TestEqual(TEXT("窗口容量为 4"), Sampler.GetCapacity(), 4);
    TestEqual(TEXT("初始采样数为 0"), Sampler.GetSampleCount(), 0);
    TestEqual(TEXT("初始平均值为 0"), Sampler.GetAverage(), 0.0);

    Sampler.AddSample(10.0);
    TestFalse(TEXT("添加样本后 IsEmpty 为假"), Sampler.IsEmpty());
    Sampler.AddSample(20.0);
    TestEqual(TEXT("已采集 2 项"), Sampler.GetSampleCount(), 2);
    TestEqual(TEXT("前两项均值为 15"), Sampler.GetAverage(), 15.0);
    TestEqual(TEXT("前两项最小值为 10"), Sampler.GetMin(), 10.0);
    TestEqual(TEXT("前两项最大值为 20"), Sampler.GetMax(), 20.0);

    Sampler.AddSample(30.0);
    Sampler.AddSample(40.0);
    TestEqual(TEXT("已采集 4 项已满"), Sampler.GetSampleCount(), 4);
    TestEqual(TEXT("4 项均值为 25"), Sampler.GetAverage(), 25.0);

    Sampler.AddSample(50.0);
    TestEqual(TEXT("窗口滑动后计数维持 4"), Sampler.GetSampleCount(), 4);
    TestEqual(TEXT("滑动后均值为 35"), Sampler.GetAverage(), 35.0);
    TestEqual(TEXT("滑动后最大值为 50"), Sampler.GetMax(), 50.0);

    Sampler.Reset();
    TestEqual(TEXT("重置后计数归零"), Sampler.GetSampleCount(), 0);
    TestEqual(TEXT("重置后均值归零"), Sampler.GetAverage(), 0.0);

    return true;
}

#endif
