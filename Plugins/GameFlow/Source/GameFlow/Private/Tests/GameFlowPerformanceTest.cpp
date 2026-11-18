/**
 * @file GameFlowPerformanceTest.cpp
 * @brief GameFlow 关卡加载状态机与就绪屏障热路径基准性能测试
 */

#include "Async/FWaitLoadingCompleteAwaiter.h"
#include "Async/TGameCoroTask.h"
#include "System/FLoadingBarrierToken.h"
#include "System/ULoadingSubsystem.h"

#if WITH_AUTOMATION_TESTS

#include <algorithm>
#include <cmath>
#include <numeric>

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 5;

constexpr int32 BarrierTokenIterations = 20000;
constexpr double BarrierTokenBudgetNsPerOp = 600.0;

constexpr int32 BarrierStackTokenIterations = 20000;
constexpr double BarrierStackTokenBudgetNsPerOp = 400.0;

constexpr int32 BarrierBpIterations = 50000;
constexpr double BarrierBpBudgetNsPerOp = 300.0;

constexpr int32 CoroPassThroughIterations = 50000;
constexpr double CoroPassThroughBudgetNsPerOp = 400.0;

constexpr int32 DelegateBroadcastIterations = 20000;
constexpr double DelegateBroadcastBudgetNsPerOp = 600.0;

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

    TArray<double> RoundNsPerOp;
    RoundNsPerOp.Reserve(SampleRounds);

    for (int32 Round = 0; Round < SampleRounds; ++Round) {
        const double StartTime = FPlatformTime::Seconds();
        Body(IterationsPerRound);
        const double EndTime = FPlatformTime::Seconds();

        const double ElapsedNs = (EndTime - StartTime) * 1e9;
        RoundNsPerOp.Add(ElapsedNs / static_cast<double>(IterationsPerRound));
    }

    RoundNsPerOp.Sort();

    FBenchmarkMetrics Metrics;
    Metrics.MinNs = RoundNsPerOp[0];
    Metrics.MaxNs = RoundNsPerOp.Last();
    Metrics.MedianNs = RoundNsPerOp[SampleRounds / 2];

    const double Sum = std::accumulate(RoundNsPerOp.GetData(), RoundNsPerOp.GetData() + SampleRounds, 0.0);
    Metrics.MeanNs = Sum / static_cast<double>(SampleRounds);

    double VarianceSum = 0.0;
    for (double Val : RoundNsPerOp) {
        const double Diff = Val - Metrics.MeanNs;
        VarianceSum += Diff * Diff;
    }
    const double StdDev = std::sqrt(VarianceSum / static_cast<double>(SampleRounds));
    Metrics.CvPercent = (Metrics.MeanNs > 0.0) ? (StdDev / Metrics.MeanNs) * 100.0 : 0.0;
    Metrics.OpsPerSec = (Metrics.MedianNs > 0.0) ? (1e9 / Metrics.MedianNs) : 0.0;

    return Metrics;
}

ULoadingSubsystem* CreateBenchmarkSubsystem(UGameInstance*& OutGameInstance) {
    if (GEngine == nullptr) {
        return nullptr;
    }
    OutGameInstance = NewObject<UGameInstance>(GEngine);
    OutGameInstance->InitializeStandalone(FName(TEXT("FlowBenchmarkWorld")));
    return OutGameInstance->GetSubsystem<ULoadingSubsystem>();
}

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
 * @brief 基准测试：验证 FLoadingBarrierToken RAII 申请与析构释放循环的纳秒级吞吐性能
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFlowBarrierTokenBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    ULoadingSubsystem* Subsystem = CreateBenchmarkSubsystem(GameInstance);
    TestNotNull(TEXT("子系统有效"), Subsystem);
    if (!Subsystem || !GameInstance) {
        return false;
    }

    const FName BarrierName(TEXT("BenchToken"));

    const FBenchmarkMetrics Metrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        BarrierTokenIterations,
        [&](int32 Iterations) {
            for (int32 i = 0; i < Iterations; ++i) {
                TSharedRef<FLoadingBarrierToken> Token = Subsystem->AcquireLoadingBarrier(BarrierName);
                Token->Release();
            }
        }
    );

    TestEqual(TEXT("测试结束后活跃屏障数为 0"), Subsystem->GetActiveBarrierCount(), 0);
    TestTrue(
        FString::Printf(
            TEXT("屏障令牌生命周期耗时 Median=%.2f ns/op 应低于预算 %.2f ns/op (Mean=%.2f, CV=%.2f%%, Ops/sec=%.0f)"),
            Metrics.MedianNs,
            BarrierTokenBudgetNsPerOp,
            Metrics.MeanNs,
            Metrics.CvPercent,
            Metrics.OpsPerSec
        ),
        Metrics.MedianNs <= BarrierTokenBudgetNsPerOp
    );

    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFlowBarrierStackTokenBenchmarkTest,
    "Game.Flow.Benchmark.BarrierStackTokenLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 基准测试：验证 FLoadingBarrierToken 栈上值类型直接申请与析构释放的零堆分配纳秒级吞吐性能
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFlowBarrierStackTokenBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    ULoadingSubsystem* Subsystem = CreateBenchmarkSubsystem(GameInstance);
    TestNotNull(TEXT("子系统有效"), Subsystem);
    if (!Subsystem || !GameInstance) {
        return false;
    }

    const FName BarrierName(TEXT("BenchStackToken"));

    const FBenchmarkMetrics Metrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        BarrierStackTokenIterations,
        [&](int32 Iterations) {
            for (int32 i = 0; i < Iterations; ++i) {
                FLoadingBarrierToken Token = Subsystem->AcquireBarrierToken(BarrierName);
                Token.Release();
            }
        }
    );

    TestEqual(TEXT("测试结束后活跃屏障数为 0"), Subsystem->GetActiveBarrierCount(), 0);
    TestTrue(
        FString::Printf(
            TEXT("栈上屏障令牌生命周期耗时 Median=%.2f ns/op 应低于预算 %.2f ns/op (Mean=%.2f, CV=%.2f%%, Ops/sec=%.0f)"),
            Metrics.MedianNs,
            BarrierStackTokenBudgetNsPerOp,
            Metrics.MeanNs,
            Metrics.CvPercent,
            Metrics.OpsPerSec
        ),
        Metrics.MedianNs <= BarrierStackTokenBudgetNsPerOp
    );

    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFlowBarrierBpBenchmarkTest,
    "Game.Flow.Benchmark.BarrierBpLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 基准测试：验证蓝图纯整数屏障申请与释放的轻量级纳秒开销
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFlowBarrierBpBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    ULoadingSubsystem* Subsystem = CreateBenchmarkSubsystem(GameInstance);
    TestNotNull(TEXT("子系统有效"), Subsystem);
    if (!Subsystem || !GameInstance) {
        return false;
    }

    const FName BarrierName(TEXT("BenchBp"));

    const FBenchmarkMetrics Metrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        BarrierBpIterations,
        [&](int32 Iterations) {
            for (int32 i = 0; i < Iterations; ++i) {
                const int64 BarrierId = Subsystem->AcquireLoadingBarrierBp(BarrierName);
                Subsystem->ReleaseLoadingBarrierBp(BarrierId);
            }
        }
    );

    TestEqual(TEXT("测试结束后活跃屏障数为 0"), Subsystem->GetActiveBarrierCount(), 0);
    TestTrue(
        FString::Printf(
            TEXT("蓝图屏障申请释放耗时 Median=%.2f ns/op 应低于预算 %.2f ns/op (Mean=%.2f, CV=%.2f%%, Ops/sec=%.0f)"),
            Metrics.MedianNs,
            BarrierBpBudgetNsPerOp,
            Metrics.MeanNs,
            Metrics.CvPercent,
            Metrics.OpsPerSec
        ),
        Metrics.MedianNs <= BarrierBpBudgetNsPerOp
    );

    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFlowCoroPassThroughBenchmarkTest,
    "Game.Flow.Benchmark.CoroPassThrough",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 基准测试：验证非加载态下 WaitLoadingComplete 协程同步直通检测开销
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFlowCoroPassThroughBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    ULoadingSubsystem* Subsystem = CreateBenchmarkSubsystem(GameInstance);
    TestNotNull(TEXT("子系统有效"), Subsystem);
    if (!Subsystem || !GameInstance) {
        return false;
    }

    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("世界有效"), World);
    if (!World) {
        GameInstance->Shutdown();
        return false;
    }

    int32 CompletedCount = 0;

    const FBenchmarkMetrics Metrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        CoroPassThroughIterations,
        [&](int32 Iterations) {
            for (int32 i = 0; i < Iterations; ++i) {
                TGameCoroTask<bool> Task = RunSyncCoroPassThrough(World);
                if (Task.IsDone() && Task.GetResult().GetValue()) {
                    ++CompletedCount;
                }
            }
        }
    );

    TestTrue(TEXT("协程均已同步完成"), CompletedCount > 0);
    TestTrue(
        FString::Printf(
            TEXT("非加载态协程直通耗时 Median=%.2f ns/op 应低于预算 %.2f ns/op (Mean=%.2f, CV=%.2f%%, Ops/sec=%.0f)"),
            Metrics.MedianNs,
            CoroPassThroughBudgetNsPerOp,
            Metrics.MeanNs,
            Metrics.CvPercent,
            Metrics.OpsPerSec
        ),
        Metrics.MedianNs <= CoroPassThroughBudgetNsPerOp
    );

    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFlowDelegateBroadcastBenchmarkTest,
    "Game.Flow.Benchmark.LoadingDelegateBroadcast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 基准测试：验证加载状态变动原生委托与动态多播委托分发开销
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFlowDelegateBroadcastBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    ULoadingSubsystem* Subsystem = CreateBenchmarkSubsystem(GameInstance);
    TestNotNull(TEXT("子系统有效"), Subsystem);
    if (!Subsystem || !GameInstance) {
        return false;
    }

    int32 NativeCallbackCount = 0;
    const FDelegateHandle Handle = Subsystem->OnLoadingStateChangedNative.AddLambda(
        [&NativeCallbackCount](bool, FName) {
            ++NativeCallbackCount;
        }
    );

    const FName TestLevel(TEXT("BenchMap"));

    const FBenchmarkMetrics Metrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        DelegateBroadcastIterations,
        [&](int32 Iterations) {
            for (int32 i = 0; i < Iterations; ++i) {
                Subsystem->OnLoadingStateChangedNative.Broadcast(true, TestLevel);
                Subsystem->OnLoadingStateChangedNative.Broadcast(false, TestLevel);
            }
        }
    );

    TestTrue(TEXT("委托回调已执行有效次数"), NativeCallbackCount > 0);
    TestTrue(
        FString::Printf(
            TEXT("委托派发双向耗时 Median=%.2f ns/op 应低于预算 %.2f ns/op (Mean=%.2f, CV=%.2f%%, Ops/sec=%.0f)"),
            Metrics.MedianNs,
            DelegateBroadcastBudgetNsPerOp,
            Metrics.MeanNs,
            Metrics.CvPercent,
            Metrics.OpsPerSec
        ),
        Metrics.MedianNs <= DelegateBroadcastBudgetNsPerOp
    );

    Subsystem->OnLoadingStateChangedNative.Remove(Handle);
    GameInstance->Shutdown();
    return true;
}

#endif
