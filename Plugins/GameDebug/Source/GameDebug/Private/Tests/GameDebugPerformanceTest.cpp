/**
 * @file GameDebugPerformanceTest.cpp
 * @brief GameDebug 全组件热路径微基准性能测试套件（2 轮预热 + 7 轮采样、中位数/CV%、防 DCE Sink 与 GameLogging Display 报告）
 */

#include "Cheat/FGameCheatRegistry.h"
#include "Config/GameCompileSwitches.h"
#include "Print/FGameDebugPrint.h"
#include "Profiling/FScopedDebugStopwatch.h"
#include "Tests/GameDebugTestUtils.h"
#include "Visualizer/FGameDebugDraw.h"
#include "Visualizer/FGameDebugTrailRecorder.h"
#include "Watcher/FGameDebugWatchHub.h"

#if WITH_AUTOMATION_TESTS

#include "HAL/PlatformTime.h"
#include "Logging/GameLogCategories.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/AutomationTest.h"

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 7;

constexpr int32 CheatHitIterations = 5000;
constexpr int32 CheatMissIterations = 20000;
constexpr double CheatHitBudgetNs = 15000.0;
constexpr double CheatMissBudgetNs = 1500.0;

constexpr int32 KeyHashIterations = 100000;
constexpr double KeyHashBudgetNs = 50.0;

constexpr int32 WatchTickIterations = 500;
constexpr double WatchTickBudgetNs = 100000.0;

constexpr int32 TrailSampleIterations = 50000;
constexpr double TrailSample64BudgetNs = 100.0;
constexpr double TrailSample256BudgetNs = 100.0;
constexpr int32 TrailDrawIterations = 500;
constexpr double TrailDraw64BudgetNs = 200000.0;

constexpr int32 StopwatchIterations = 50000;
constexpr double StopwatchFastPathBudgetNs = 200.0;

constexpr int32 DrawFastRejectIterations = 100000;
constexpr double DrawFastRejectBudgetNs = 50.0;

struct FBenchmarkMetrics {
    double MedianNs = 0.0;
    double MeanNs = 0.0;
    double CvPercent = 0.0;
    double MinNs = 0.0;
    double MaxNs = 0.0;
    double OpsPerSec = 0.0;
};

template <typename TFunc>
FBenchmarkMetrics RunBenchmarkRounds(
    int32 WarmupRounds,
    int32 SampleRounds,
    int32 IterationsPerRound,
    TFunc&& Body
) {
    for (int32 Round = 0; Round < WarmupRounds; ++Round) {
        Body(IterationsPerRound);
    }

    TArray<double> SamplesNs;
    SamplesNs.Reserve(SampleRounds);

    double SumNs = 0.0;
    for (int32 Round = 0; Round < SampleRounds; ++Round) {
        const double StartTime = FPlatformTime::Seconds();
        Body(IterationsPerRound);
        const double ElapsedSec = FPlatformTime::Seconds() - StartTime;
        const double NsPerOp = (ElapsedSec * 1.0e9) / static_cast<double>(IterationsPerRound);
        SamplesNs.Add(NsPerOp);
        SumNs += NsPerOp;
    }

    SamplesNs.Sort();
    const double MedianNs = SamplesNs[SampleRounds / 2];
    const double MeanNs = SumNs / static_cast<double>(SampleRounds);

    double VarianceSum = 0.0;
    for (const double Val : SamplesNs) {
        const double Diff = Val - MeanNs;
        VarianceSum += Diff * Diff;
    }
    const double StdDev = FMath::Sqrt(VarianceSum / static_cast<double>(SampleRounds));

    FBenchmarkMetrics Metrics;
    Metrics.MinNs = SamplesNs[0];
    Metrics.MaxNs = SamplesNs.Last();
    Metrics.MedianNs = MedianNs;
    Metrics.MeanNs = MeanNs;
    Metrics.CvPercent = (MeanNs > 0.0) ? ((StdDev / MeanNs) * 100.0) : 0.0;
    Metrics.OpsPerSec = (MedianNs > 0.0) ? (1.0e9 / MedianNs) : 0.0;
    return Metrics;
}

void RecordBenchmarkResult(
    FAutomationTestBase* Test,
    const TCHAR* TestName,
    const FBenchmarkMetrics& Metrics,
    double BudgetNs
) {
    const FString Msg = FString::Printf(
        TEXT("[DebugPerf] %s: 中位数 %.2f ns (预算 %.0f ns), 均值 %.2f ns, 离散度 CV=%.2f%%, 吞吐量 %.0f ops/s"),
        TestName,
        Metrics.MedianNs,
        BudgetNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec
    );
    GAME_LOG(Display, TEXT("%s"), *Msg);
    Test->AddInfo(Msg);
    Test->TestTrue(Msg, Metrics.MedianNs <= BudgetNs);
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameCheatRegistryBenchmark,
    "Game.Debug.Benchmark.CheatRegistryThroughput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准：GM 指令命中引号分词派发与未命中零分配短路吞吐
 * @param Parameters 自动化测试参数
 * @return 全部预算断言通过返回 true
 */
bool FGameCheatRegistryBenchmark::RunTest([[maybe_unused]] const FString& Parameters) {
    if constexpr (!Game::Config::bEnableGM) {
        return true;
    }

    FGameCheatRegistry& Registry = FGameCheatRegistry::Get();
    const FName BenchmarkCmd(TEXT("Benchmark.GM.TestCmd"));
    Registry.UnregisterCheat(BenchmarkCmd);

    volatile int64 ArgCountSink = 0;
    Registry.RegisterCheat(
        BenchmarkCmd,
        TEXT("Benchmark command"),
        [&ArgCountSink](const TArray<FString>& Args) {
            ArgCountSink = ArgCountSink + Args.Num();
        }
    );

    const FStringView HitCmdLine(TEXT("Benchmark.GM.TestCmd \"Arg One\" ArgTwo 12345"));
    const FBenchmarkMetrics HitMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        CheatHitIterations,
        [&](int32 Iterations) {
            for (int32 i = 0; i < Iterations; ++i) {
                (void)Registry.ExecuteCheat(HitCmdLine);
            }
        }
    );
    RecordBenchmarkResult(this, TEXT("GM 指令命中分词+锁外派发"), HitMetrics, CheatHitBudgetNs);

    const FStringView MissCmdLine(TEXT("Benchmark.GM.UnregisteredCmd \"Heavy Ignored Arg\" 99999"));
    volatile int64 MissSink = 0;
    const FBenchmarkMetrics MissMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        CheatMissIterations,
        [&](int32 Iterations) {
            int64 Misses = 0;
            for (int32 i = 0; i < Iterations; ++i) {
                if (!Registry.ExecuteCheat(MissCmdLine)) {
                    ++Misses;
                }
            }
            MissSink = MissSink + Misses;
        }
    );
    RecordBenchmarkResult(this, TEXT("GM 指令未命中零分配短路"), MissMetrics, CheatMissBudgetNs);

    Registry.UnregisterCheat(BenchmarkCmd);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugPrintKeyBenchmark,
    "Game.Debug.Benchmark.PrintChannelKeyHash",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准：多通道轮转计算 64 位域盐屏幕槽位 Key 的散列吞吐（防循环不变量折叠）
 * @param Parameters 自动化测试参数
 * @return 全部预算断言通过返回 true
 */
bool FGameDebugPrintKeyBenchmark::RunTest([[maybe_unused]] const FString& Parameters) {
    TArray<FName> Channels;
    Channels.Reserve(16);
    for (int32 Idx = 0; Idx < 16; ++Idx) {
        Channels.Add(FName(*FString::Printf(TEXT("BenchPrintChannel_%d"), Idx)));
    }

    volatile uint64 KeySink = 0;
    const FBenchmarkMetrics Metrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        KeyHashIterations,
        [&](int32 Iterations) {
            uint64 LocalSum = 0;
            for (int32 i = 0; i < Iterations; ++i) {
                LocalSum ^= FGameDebugPrint::GetChannelKey(Channels[i & 15]);
            }
            KeySink = KeySink ^ LocalSum;
        }
    );

    RecordBenchmarkResult(this, TEXT("Print 64位域盐槽位 Key 雪崩散列"), Metrics, KeyHashBudgetNs);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugWatchHubTickBenchmark,
    "Game.Debug.Benchmark.WatchHubSamplingThroughput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准：调试观察中心 5 分类 50 观察项全量快照采样与屏幕刷新开销
 * @param Parameters 自动化测试参数
 * @return 全部预算断言通过返回 true
 */
bool FGameDebugWatchHubTickBenchmark::RunTest([[maybe_unused]] const FString& Parameters) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return true;
    }

    Game::Debug::Test::FScopedScreenMessageGuard ScreenGuard;
    FGameDebugWatchHub& Hub = FGameDebugWatchHub::Get();
    Hub.ClearAll();
    Hub.SetGlobalEnabled(true);

    constexpr int32 NumCategories = 5;
    constexpr int32 ItemsPerCategory = 10;

    int32 SampleCounter = 0;
    for (int32 CatIdx = 0; CatIdx < NumCategories; ++CatIdx) {
        const FName CatName(*FString::Printf(TEXT("BenchWatchCat_%d"), CatIdx));
        for (int32 ItemIdx = 0; ItemIdx < ItemsPerCategory; ++ItemIdx) {
            const FName ItemName(*FString::Printf(TEXT("Item_%d"), ItemIdx));
            Hub.Watch(CatName, ItemName, [&SampleCounter]() {
                return FString::FromInt(++SampleCounter);
            });
        }
    }

    const FBenchmarkMetrics Metrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        WatchTickIterations,
        [&](int32 Iterations) {
            for (int32 i = 0; i < Iterations; ++i) {
                Hub.TickDisplay(0.016f);
            }
        }
    );

    RecordBenchmarkResult(this, TEXT("WatchHub 5组50项快照采样+格式化"), Metrics, WatchTickBudgetNs);

    Hub.ClearAll();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugTrailRecorderBenchmark,
    "Game.Debug.Benchmark.TrailRecorderSamplingThroughput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准：轨迹记录器 O(1) 环形缓冲（Cap=64 / Cap=256）高频采样与 64 段绘制吞吐
 * @param Parameters 自动化测试参数
 * @return 全部预算断言通过返回 true
 */
bool FGameDebugTrailRecorderBenchmark::RunTest([[maybe_unused]] const FString& Parameters) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return true;
    }

    volatile int64 SampleSink = 0;

    FGameDebugTrailRecorder Recorder64(64, 2.0f, 1.0f);
    double SimTime64 = 100.0;
    const FBenchmarkMetrics Metrics64 = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        TrailSampleIterations,
        [&](int32 Iterations) {
            int64 LocalAccepted = 0;
            for (int32 i = 0; i < Iterations; ++i) {
                SimTime64 += 0.01;
                if (Recorder64.AddSample(FVector(static_cast<double>(i) * 5.0, 0.0, 0.0), SimTime64)) {
                    ++LocalAccepted;
                }
            }
            SampleSink = SampleSink + LocalAccepted;
        }
    );
    RecordBenchmarkResult(this, TEXT("TrailRecorder O(1) 环形写入 (Cap=64)"), Metrics64, TrailSample64BudgetNs);

    FGameDebugTrailRecorder Recorder256(256, 5.0f, 1.0f);
    double SimTime256 = 100.0;
    const FBenchmarkMetrics Metrics256 = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        TrailSampleIterations,
        [&](int32 Iterations) {
            int64 LocalAccepted = 0;
            for (int32 i = 0; i < Iterations; ++i) {
                SimTime256 += 0.01;
                if (Recorder256.AddSample(FVector(static_cast<double>(i) * 5.0, 0.0, 0.0), SimTime256)) {
                    ++LocalAccepted;
                }
            }
            SampleSink = SampleSink + LocalAccepted;
        }
    );
    RecordBenchmarkResult(this, TEXT("TrailRecorder O(1) 环形写入 (Cap=256)"), Metrics256, TrailSample256BudgetNs);

    if (UWorld* World = Game::Debug::Test::GetAutomationTestWorld()) {
        FGameDebugTrailRecorder DrawRecorder(64, 10.0f, 1.0f);
        for (int32 i = 0; i < 64; ++i) {
            DrawRecorder.AddSample(FVector(static_cast<double>(i) * 20.0, 0.0, 0.0), 100.0 + static_cast<double>(i) * 0.05);
        }
        const FBenchmarkMetrics DrawMetrics = RunBenchmarkRounds(
            DefaultWarmupRounds,
            DefaultSampleRounds,
            TrailDrawIterations,
            [&](int32 Iterations) {
                for (int32 i = 0; i < Iterations; ++i) {
                    DrawRecorder.DrawTrail(World, FColor::Green, FColor::Blue, 2.0f, 0.0f, 103.5);
                }
                Game::Debug::Test::FlushWorldDebugLines(World);
            }
        );
        RecordBenchmarkResult(this, TEXT("TrailRecorder 64点时间渐变绘制 (DrawTrail)"), DrawMetrics, TrailDraw64BudgetNs);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugStopwatchAndDrawBenchmark,
    "Game.Debug.Benchmark.StopwatchAndDrawFastPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准：FScopedDebugStopwatch 未超时快路径（Cycles64）与 FGameDebugDraw 边界快速拦截吞吐
 * @param Parameters 自动化测试参数
 * @return 全部预算断言通过返回 true
 */
bool FGameDebugStopwatchAndDrawBenchmark::RunTest([[maybe_unused]] const FString& Parameters) {
    if constexpr (Game::Config::bEnableCpuProfiler) {
        const FName BenchScope(TEXT("BenchStopwatchFastPath"));
        const FName BenchChannel(TEXT("Profiler"));
        volatile double ElapsedSink = 0.0;

        const FBenchmarkMetrics StopwatchMetrics = RunBenchmarkRounds(
            DefaultWarmupRounds,
            DefaultSampleRounds,
            StopwatchIterations,
            [&](int32 Iterations) {
                double LocalSum = 0.0;
                for (int32 i = 0; i < Iterations; ++i) {
                    FScopedDebugStopwatch Watch(BenchScope, 100000.0, BenchChannel);
                    LocalSum += Watch.GetElapsedMs();
                }
                ElapsedSink = ElapsedSink + LocalSum;
            }
        );
        RecordBenchmarkResult(this, TEXT("ScopedDebugStopwatch 未超时快路径 (Cycles64)"), StopwatchMetrics, StopwatchFastPathBudgetNs);
    }

    if constexpr (Game::Config::bEnableDebugDraw) {
        const FVector Start = FVector::ZeroVector;
        const FVector End(100.0, 0.0, 0.0);
        const FBenchmarkMetrics RejectMetrics = RunBenchmarkRounds(
            DefaultWarmupRounds,
            DefaultSampleRounds,
            DrawFastRejectIterations,
            [&](int32 Iterations) {
                for (int32 i = 0; i < Iterations; ++i) {
                    FGameDebugDraw::DrawLine(nullptr, Start, End);
                }
            }
        );
        RecordBenchmarkResult(this, TEXT("FGameDebugDraw 空世界快速拦截"), RejectMetrics, DrawFastRejectBudgetNs);
    }

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
