/**
 * @file GameDebugPerformanceTest.cpp
 * @brief GameDebug 模块基准性能测试（GM 指令吞吐、通道 Key 哈希计算、观察中心采样开销）
 */

#include "Cheat/FGameCheatRegistry.h"
#include "Config/GameCompileSwitches.h"
#include "Print/FGameDebugPrint.h"
#include "Visualizer/FGameDebugTrailRecorder.h"
#include "Watcher/FGameDebugWatchHub.h"

#if WITH_AUTOMATION_TESTS

#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameCheatRegistryBenchmark,
    "Game.Debug.Benchmark.CheatRegistryThroughput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 压测 GM 指令高频参数分词与锁外安全派发吞吐性能
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameCheatRegistryBenchmark::RunTest([[maybe_unused]] const FString& Parameters) {
    if constexpr (!Game::Config::bEnableGM) {
        return true;
    }

    FGameCheatRegistry& Registry = FGameCheatRegistry::Get();
    const FName BenchmarkCmd(TEXT("Benchmark.GM.TestCmd"));
    Registry.UnregisterCheat(BenchmarkCmd);

    int64 ExecCount = 0;
    Registry.RegisterCheat(
        BenchmarkCmd,
        TEXT("Benchmark command"),
        [&ExecCount](const TArray<FString>& Args) {
            ++ExecCount;
        }
    );

    constexpr int32 Iterations = 10000;
    const FString CmdLine = TEXT("Benchmark.GM.TestCmd \"Arg One\" ArgTwo 12345");

    const double StartTime = FPlatformTime::Seconds();
    for (int32 i = 0; i < Iterations; ++i) {
        Registry.ExecuteCheat(CmdLine);
    }
    const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
    const double AvgUsPerOp = (ElapsedMs * 1000.0) / Iterations;

    TestEqual(TEXT("高频指令全部命中派发"), ExecCount, static_cast<int64>(Iterations));
    TestTrue(TEXT("单次分词与派发平均耗时小于 20 微秒"), AvgUsPerOp < 20.0);

    Registry.UnregisterCheat(BenchmarkCmd);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugPrintKeyBenchmark,
    "Game.Debug.Benchmark.PrintChannelKeyHash",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 压测通道名称生成 64 位槽位 Key 的哈希计算性能
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameDebugPrintKeyBenchmark::RunTest([[maybe_unused]] const FString& Parameters) {
    const FName ChannelCombat(TEXT("CombatDamageTrackerChannel"));
    constexpr int32 Iterations = 100000;

    uint64 DummySum = 0;
    const double StartTime = FPlatformTime::Seconds();
    for (int32 i = 0; i < Iterations; ++i) {
        DummySum += FGameDebugPrint::GetChannelKey(ChannelCombat);
    }
    const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
    const double AvgNsPerOp = (ElapsedMs * 1000000.0) / Iterations;

    TestTrue(TEXT("通道 Key 计算产出有效非零值"), DummySum > 0);
    TestTrue(TEXT("单次通道 Key 计算平均耗时小于 50 纳秒"), AvgNsPerOp < 50.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugWatchHubTickBenchmark,
    "Game.Debug.Benchmark.WatchHubSamplingThroughput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 压测调试观察中心多分类变量拉取与快照采样的帧开销
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameDebugWatchHubTickBenchmark::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameDebugWatchHub& Hub = FGameDebugWatchHub::Get();
    Hub.ClearAll();

    constexpr int32 NumCategories = 5;
    constexpr int32 ItemsPerCategory = 10;

    int32 SampleCounter = 0;
    for (int32 c = 0; c < NumCategories; ++c) {
        const FName CatName(*FString::Printf(TEXT("BenchCat_%d"), c));
        for (int32 item = 0; item < ItemsPerCategory; ++item) {
            const FName ItemName(*FString::Printf(TEXT("Item_%d"), item));
            Hub.Watch(CatName, ItemName, [&SampleCounter]() {
                return FString::FromInt(++SampleCounter);
            });
        }
    }

    constexpr int32 TickIterations = 1000;
    const double StartTime = FPlatformTime::Seconds();
    for (int32 i = 0; i < TickIterations; ++i) {
        Hub.TickDisplay(0.016f);
    }
    const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
    const double AvgUsPerTick = (ElapsedMs * 1000.0) / TickIterations;

    TestTrue(TEXT("50 个观察变量单次全量采样平均耗时小于 100 微秒"), AvgUsPerTick < 100.0);

    Hub.ClearAll();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugTrailRecorderBenchmark,
    "Game.Debug.Benchmark.TrailRecorderSamplingThroughput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 压测空间运动轨迹记录器高频添加采样点与滚动淘汰的极限纳秒吞吐
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameDebugTrailRecorderBenchmark::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameDebugTrailRecorder Recorder(64, 2.0f, 1.0f);
    constexpr int32 Iterations = 100000;

    int32 SampleSuccessCount = 0;
    const double StartTime = FPlatformTime::Seconds();
    for (int32 i = 0; i < Iterations; ++i) {
        const FVector Pos(static_cast<double>(i) * 5.0, 0.0, 0.0);
        if (Recorder.AddSample(Pos, 100.0 + static_cast<double>(i) * 0.01)) {
            ++SampleSuccessCount;
        }
    }
    const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
    const double AvgNsPerOp = (ElapsedMs * 1000000.0) / Iterations;

    TestEqual(TEXT("高频采样全部成功"), SampleSuccessCount, Iterations);
    TestTrue(TEXT("单次采样与滚动淘汰平均耗时小于 100 纳秒"), AvgNsPerOp < 100.0);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
