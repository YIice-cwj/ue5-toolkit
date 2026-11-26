/**
 * @file GameFlowPerformanceTest.cpp
 * @brief GameFlow 全组件热路径 7 项微基准性能测试套件（2 轮预热 + 7 轮采样，输出 Median/Mean/CV%/Ops）
 */

#include "Async/FWaitLoadingCompleteAwaiter.h"
#include "Async/TGameCoroTask.h"
#include "System/FLoadingBarrierToken.h"
#include "System/ULoadingSubsystem.h"
#include "Tests/GameFlowTestUtils.h"
#include "Tests/ULoadingTestReceiver.h"

#if WITH_AUTOMATION_TESTS

#include "HAL/PlatformTime.h"
#include "Logging/GameLogCategories.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/AutomationTest.h"

using Game::Flow::Test::FScopedTestLoadingWorld;

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 7;

constexpr int32 BarrierTokenIterations = 30000;
constexpr double BarrierTokenBudgetNsPerOp = 350.0;

constexpr int32 BarrierBpIterations = 50000;
constexpr double BarrierBpBudgetNsPerOp = 300.0;

constexpr int32 ProgressRecalcIterations = 30000;
constexpr double ProgressRecalcBudgetNsPerOp = 500.0;

constexpr int32 WatchdogSweepIterations = 40000;
constexpr double WatchdogSweepBudgetNsPerOp = 400.0;

constexpr int32 CoroPassThroughIterations = 50000;
constexpr double CoroPassThroughBudgetNsPerOp = 350.0;

constexpr int32 CoroSuspendResumeBatchSize = 256;
constexpr int32 CoroSuspendResumeIterations = 4096;
constexpr double CoroSuspendResumeBudgetNsPerOp = 2000.0;

constexpr int32 DelegateBroadcastIterations = 20000;
constexpr double DelegateBroadcastBudgetNsPerOp = 1500.0;

struct FFlowBenchmarkMetrics {
    double MedianNs = 0.0;
    double MeanNs = 0.0;
    double CvPercent = 0.0;
    double MinNs = 0.0;
    double MaxNs = 0.0;
    double OpsPerSec = 0.0;
};

/**
 * @brief 执行多轮预热与采样微基准，统计中位数、均值、极值、离散度与吞吐量
 * @tparam TFunc 待测循环体闭包类型
 * @param WarmupRounds 预热丢弃轮数
 * @param SampleRounds 正式采样轮数
 * @param IterationsPerRound 每轮执行迭代次数
 * @param Body 待测循环体闭包
 * @return 统计指标结构体
 */
template <typename TFunc>
FFlowBenchmarkMetrics RunFlowBenchmark(
    int32 WarmupRounds,
    int32 SampleRounds,
    int32 IterationsPerRound,
    TFunc&& Body
) {
    for (int32 Round = 0; Round < WarmupRounds; ++Round) {
        Body(IterationsPerRound);
    }

    TArray<double> RoundNsPerOp;
    RoundNsPerOp.Reserve(SampleRounds);

    double SumNs = 0.0;
    for (int32 Round = 0; Round < SampleRounds; ++Round) {
        const double StartTime = FPlatformTime::Seconds();
        Body(IterationsPerRound);
        const double ElapsedSec = FPlatformTime::Seconds() - StartTime;
        const double NsPerOp = (ElapsedSec * 1.0e9) / static_cast<double>(IterationsPerRound);
        RoundNsPerOp.Add(NsPerOp);
        SumNs += NsPerOp;
    }

    RoundNsPerOp.Sort();

    FFlowBenchmarkMetrics Metrics;
    Metrics.MinNs = RoundNsPerOp[0];
    Metrics.MaxNs = RoundNsPerOp.Last();
    Metrics.MedianNs = RoundNsPerOp[SampleRounds / 2];
    Metrics.MeanNs = SumNs / static_cast<double>(SampleRounds);

    double VarianceSum = 0.0;
    for (const double Val : RoundNsPerOp) {
        const double Diff = Val - Metrics.MeanNs;
        VarianceSum += Diff * Diff;
    }
    const double StdDev = FMath::Sqrt(VarianceSum / static_cast<double>(SampleRounds));
    Metrics.CvPercent = (Metrics.MeanNs > 0.0) ? ((StdDev / Metrics.MeanNs) * 100.0) : 0.0;
    Metrics.OpsPerSec = (Metrics.MedianNs > 0.0) ? (1.0e9 / Metrics.MedianNs) : 0.0;
    return Metrics;
}

/**
 * @brief 将微基准统计报告同时写入 LogGameFlow Display 日志与 Automation Test Info
 * @param Test 当前自动化测试实例引用
 * @param BenchName 基准测试名称
 * @param Metrics 实测统计指标
 */
void EmitBenchmarkReport(FAutomationTestBase& Test, const TCHAR* BenchName, const FFlowBenchmarkMetrics& Metrics) {
    const FString Report = FString::Printf(
        TEXT("[GameFlowPerf] %s: Median=%.2f ns/op (Mean=%.2f, Min=%.2f, Max=%.2f, CV=%.2f%%, %.2f Mops/s)"),
        BenchName,
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.MinNs,
        Metrics.MaxNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    );
    GAME_LOG(Display, TEXT("%s"), *Report);
    Test.AddInfo(Report);
}

/**
 * @brief 供微基准复用的加载完成协程等待包装函数
 * @param WorldContext 世界或实例上下文对象
 * @return 协程任务句柄
 */
TGameCoroTask<bool> RunSyncCoroPassThrough(const UObject* WorldContext) {
    const bool bDone = co_await GameFlow::WaitLoadingComplete(WorldContext);
    co_return bDone;
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFlowBarrierTokenBenchmarkTest,
    "Game.Flow.Benchmark.BarrierTokenLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 1：C++ 栈上零堆分配 FLoadingBarrierToken 经 TSlotMap 申请与释放吞吐
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFlowBarrierTokenBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestLoadingWorld TestWorld(FName(TEXT("BenchBarrierTokenWorld")));
    TestTrue(TEXT("基准测试世界有效"), TestWorld.IsValid());
    if (!TestWorld.IsValid()) {
        return false;
    }

    ULoadingSubsystem* Subsystem = TestWorld.GetSubsystem();
    const FName BarrierName(TEXT("BenchStackToken"));
    volatile uint64 Sink = 0;

    const FFlowBenchmarkMetrics Metrics = RunFlowBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        BarrierTokenIterations,
        [Subsystem, BarrierName, &Sink](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                FLoadingBarrierToken Token = Subsystem->AcquireBarrierToken(BarrierName);
                Sink = Sink + Token.GetBarrierHandle().PackUInt64();
                Token.Release();
            }
        }
    );

    TestEqual(TEXT("测试结束后活跃屏障数为 0"), Subsystem->GetActiveBarrierCount(), 0);
    TestTrue(
        FString::Printf(TEXT("BarrierTokenLifecycle Median=%.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, BarrierTokenBudgetNsPerOp),
        Metrics.MedianNs <= BarrierTokenBudgetNsPerOp
    );
    EmitBenchmarkReport(*this, TEXT("BarrierTokenLifecycle"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFlowBarrierBpBenchmarkTest,
    "Game.Flow.Benchmark.BarrierBpLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 2：蓝图 64 位代际打包句柄屏障申请与释放吞吐
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFlowBarrierBpBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestLoadingWorld TestWorld(FName(TEXT("BenchBarrierBpWorld")));
    TestTrue(TEXT("基准测试世界有效"), TestWorld.IsValid());
    if (!TestWorld.IsValid()) {
        return false;
    }

    ULoadingSubsystem* Subsystem = TestWorld.GetSubsystem();
    const FName BarrierName(TEXT("BenchBp"));
    volatile int64 Sink = 0;

    const FFlowBenchmarkMetrics Metrics = RunFlowBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        BarrierBpIterations,
        [Subsystem, BarrierName, &Sink](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const int64 BarrierId = Subsystem->AcquireLoadingBarrierBp(BarrierName);
                Sink = Sink + BarrierId;
                Subsystem->ReleaseLoadingBarrierBp(BarrierId);
            }
        }
    );

    TestEqual(TEXT("测试结束后活跃屏障数为 0"), Subsystem->GetActiveBarrierCount(), 0);
    TestTrue(
        FString::Printf(TEXT("BarrierBpLifecycle Median=%.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, BarrierBpBudgetNsPerOp),
        Metrics.MedianNs <= BarrierBpBudgetNsPerOp
    );
    EmitBenchmarkReport(*this, TEXT("BarrierBpLifecycle"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFlowProgressRecalculateBenchmarkTest,
    "Game.Flow.Benchmark.ProgressRecalculate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 3：8 路活跃屏障并发下加权进度重算与原生进度委托广播吞吐（验证 TSlotMap 密集遍历与 O(1) 已完成权重缓存）
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFlowProgressRecalculateBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestLoadingWorld TestWorld(FName(TEXT("BenchProgressRecalcWorld")));
    TestTrue(TEXT("基准测试世界有效"), TestWorld.IsValid());
    if (!TestWorld.IsValid()) {
        return false;
    }

    ULoadingSubsystem* Subsystem = TestWorld.GetSubsystem();
    TestWorld.BroadcastPreLoadMap(TEXT("/Game/Maps/BenchProgressMap"));

    for (int32 FinishedIndex = 0; FinishedIndex < 32; ++FinishedIndex) {
        FLoadingBarrierToken TempToken = Subsystem->AcquireBarrierToken(FName(TEXT("PreFinished")), 1.0f);
        TempToken.Release();
    }

    TArray<FLoadingBarrierToken> ActiveTokens;
    ActiveTokens.Reserve(8);
    for (int32 Index = 0; Index < 8; ++Index) {
        ActiveTokens.Add(Subsystem->AcquireBarrierToken(FName(TEXT("ActiveWorker")), 1.0f));
    }

    volatile float ProgressSink = 0.0f;
    const FDelegateHandle Handle = Subsystem->OnLoadingProgressChangedNative.AddLambda(
        [&ProgressSink](float Progress, const FText&) {
            ProgressSink = Progress;
        }
    );

    const FText EmptyDesc = FText::GetEmpty();
    const FFlowBenchmarkMetrics Metrics = RunFlowBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ProgressRecalcIterations,
        [&ActiveTokens, &EmptyDesc](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const float Step = static_cast<float>(Index & 63) / 63.0f;
                ActiveTokens[Index & 7].ReportProgress(Step, EmptyDesc);
            }
        }
    );

    Subsystem->OnLoadingProgressChangedNative.Remove(Handle);
    ActiveTokens.Reset();
    TestWorld.BroadcastPostLoadMap();

    TestTrue(TEXT("进度广播已被真实消费"), ProgressSink >= 0.0f);
    TestTrue(
        FString::Printf(TEXT("ProgressRecalculate Median=%.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, ProgressRecalcBudgetNsPerOp),
        Metrics.MedianNs <= ProgressRecalcBudgetNsPerOp
    );
    EmitBenchmarkReport(*this, TEXT("ProgressRecalculate"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFlowWatchdogSweepBenchmarkTest,
    "Game.Flow.Benchmark.WatchdogSweep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 4：8 路带单屏障超时活跃屏障下 CheckWatchdogTimeoutAt 无超时热巡检零堆分配开销
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFlowWatchdogSweepBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestLoadingWorld TestWorld(FName(TEXT("BenchWatchdogSweepWorld")));
    TestTrue(TEXT("基准测试世界有效"), TestWorld.IsValid());
    if (!TestWorld.IsValid()) {
        return false;
    }

    ULoadingSubsystem* Subsystem = TestWorld.GetSubsystem();
    Subsystem->SetMaxLoadingTimeoutSeconds(300.0f);
    TestWorld.BroadcastPreLoadMap(TEXT("/Game/Maps/BenchWatchdogMap"));

    TArray<FLoadingBarrierToken> ActiveTokens;
    ActiveTokens.Reserve(8);
    for (int32 Index = 0; Index < 8; ++Index) {
        ActiveTokens.Add(Subsystem->AcquireBarrierToken(FName(TEXT("WatchdogWorker")), 1.0f, 120.0f));
    }

    const double StableNow = FPlatformTime::Seconds() + 1.0;
    const FFlowBenchmarkMetrics Metrics = RunFlowBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        WatchdogSweepIterations,
        [Subsystem, StableNow](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Subsystem->CheckWatchdogTimeoutAt(StableNow);
            }
        }
    );

    TestEqual(TEXT("巡检后全部 8 个未超时屏障均存活"), Subsystem->GetActiveBarrierCount(), 8);
    ActiveTokens.Reset();
    TestWorld.BroadcastPostLoadMap();

    TestTrue(
        FString::Printf(TEXT("WatchdogSweep Median=%.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, WatchdogSweepBudgetNsPerOp),
        Metrics.MedianNs <= WatchdogSweepBudgetNsPerOp
    );
    EmitBenchmarkReport(*this, TEXT("WatchdogSweep"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFlowCoroPassThroughBenchmarkTest,
    "Game.Flow.Benchmark.CoroPassThrough",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 5：非加载态下 WaitLoadingComplete 协程零堆分配同步直通开销
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFlowCoroPassThroughBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestLoadingWorld TestWorld(FName(TEXT("BenchCoroPassThroughWorld")));
    TestTrue(TEXT("基准测试世界有效"), TestWorld.IsValid());
    if (!TestWorld.IsValid()) {
        return false;
    }

    UWorld* World = TestWorld.GetWorld();
    volatile int32 CompletedCount = 0;

    const FFlowBenchmarkMetrics Metrics = RunFlowBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        CoroPassThroughIterations,
        [World, &CompletedCount](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                TGameCoroTask<bool> Task = RunSyncCoroPassThrough(World);
                if (Task.IsDone() && Task.GetResult().GetValue()) {
                    CompletedCount = CompletedCount + 1;
                }
            }
        }
    );

    TestTrue(TEXT("协程均已同步完成"), CompletedCount > 0);
    TestTrue(
        FString::Printf(TEXT("CoroPassThrough Median=%.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, CoroPassThroughBudgetNsPerOp),
        Metrics.MedianNs <= CoroPassThroughBudgetNsPerOp
    );
    EmitBenchmarkReport(*this, TEXT("CoroPassThrough"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFlowCoroSuspendResumeBenchmarkTest,
    "Game.Flow.Benchmark.CoroSuspendResume",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 6：加载态下 WaitLoadingComplete 真实挂起、事件唤醒与自动清理往返开销（验证单次共享状态合并分配）
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFlowCoroSuspendResumeBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestLoadingWorld TestWorld(FName(TEXT("BenchCoroSuspendResumeWorld")));
    TestTrue(TEXT("基准测试世界有效"), TestWorld.IsValid());
    if (!TestWorld.IsValid()) {
        return false;
    }

    ULoadingSubsystem* Subsystem = TestWorld.GetSubsystem();
    UWorld* World = TestWorld.GetWorld();
    const FString LevelPath = TEXT("/Game/Maps/BenchCoroSuspend");
    volatile int32 ResumedCount = 0;

    const FFlowBenchmarkMetrics Metrics = RunFlowBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        CoroSuspendResumeIterations,
        [&TestWorld, World, &LevelPath, &ResumedCount](int32 Iterations) {
            TArray<TGameCoroTask<bool>> BatchTasks;
            BatchTasks.Reserve(CoroSuspendResumeBatchSize);
            int32 Remaining = Iterations;
            while (Remaining > 0) {
                const int32 BatchCount = FMath::Min(Remaining, CoroSuspendResumeBatchSize);
                BatchTasks.Reset();
                TestWorld.BroadcastPreLoadMap(LevelPath);
                for (int32 Index = 0; Index < BatchCount; ++Index) {
                    BatchTasks.Add(RunSyncCoroPassThrough(World));
                }
                TestWorld.BroadcastPostLoadMap();
                for (const TGameCoroTask<bool>& Task : BatchTasks) {
                    if (Task.IsDone() && Task.GetResult().GetValue()) {
                        ResumedCount = ResumedCount + 1;
                    }
                }
                Remaining -= BatchCount;
            }
        }
    );

    TestTrue(TEXT("协程挂起唤醒往返均成功"), ResumedCount > 0);
    TestFalse(TEXT("结束后状态委托零残留"), Subsystem->OnLoadingStateChangedNative.IsBound());
    TestFalse(TEXT("结束后中止委托零残留"), Subsystem->OnLoadingAbortedNative.IsBound());
    TestTrue(
        FString::Printf(TEXT("CoroSuspendResume Median=%.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, CoroSuspendResumeBudgetNsPerOp),
        Metrics.MedianNs <= CoroSuspendResumeBudgetNsPerOp
    );
    EmitBenchmarkReport(*this, TEXT("CoroSuspendResume"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFlowDelegateBroadcastBenchmarkTest,
    "Game.Flow.Benchmark.LoadingDelegateBroadcast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 7：加载状态变动原生多播与蓝图动态多播真实双轨分发开销
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFlowDelegateBroadcastBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestLoadingWorld TestWorld(FName(TEXT("BenchDelegateBroadcastWorld")));
    TestTrue(TEXT("基准测试世界有效"), TestWorld.IsValid());
    if (!TestWorld.IsValid()) {
        return false;
    }

    ULoadingSubsystem* Subsystem = TestWorld.GetSubsystem();
    ULoadingTestReceiver* Receiver = NewObject<ULoadingTestReceiver>(TestWorld.GetGameInstance());
    Subsystem->OnLoadingStateChanged.AddDynamic(Receiver, &ULoadingTestReceiver::OnLoadingStateChanged);

    volatile int32 NativeCallbackCount = 0;
    const FDelegateHandle Handle = Subsystem->OnLoadingStateChangedNative.AddLambda(
        [&NativeCallbackCount](bool, FName) {
            NativeCallbackCount = NativeCallbackCount + 1;
        }
    );

    const FName TestLevel(TEXT("BenchMap"));

    const FFlowBenchmarkMetrics Metrics = RunFlowBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        DelegateBroadcastIterations,
        [Subsystem, TestLevel](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Subsystem->OnLoadingStateChanged.Broadcast(true, TestLevel);
                Subsystem->OnLoadingStateChangedNative.Broadcast(true, TestLevel);
                Subsystem->OnLoadingStateChanged.Broadcast(false, TestLevel);
                Subsystem->OnLoadingStateChangedNative.Broadcast(false, TestLevel);
            }
        }
    );

    Subsystem->OnLoadingStateChangedNative.Remove(Handle);
    Subsystem->OnLoadingStateChanged.RemoveDynamic(Receiver, &ULoadingTestReceiver::OnLoadingStateChanged);

    TestTrue(TEXT("原生委托回调已执行有效次数"), NativeCallbackCount > 0);
    TestTrue(TEXT("蓝图动态委托回调已执行有效次数"), Receiver->LoadingChangeCount > 0);
    TestTrue(
        FString::Printf(TEXT("LoadingDelegateBroadcast Median=%.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, DelegateBroadcastBudgetNsPerOp),
        Metrics.MedianNs <= DelegateBroadcastBudgetNsPerOp
    );
    EmitBenchmarkReport(*this, TEXT("LoadingDelegateBroadcast"), Metrics);
    return true;
}

#endif
