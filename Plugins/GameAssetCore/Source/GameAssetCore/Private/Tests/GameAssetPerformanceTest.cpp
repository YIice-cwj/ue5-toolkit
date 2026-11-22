/**
 * @file GameAssetPerformanceTest.cpp
 * @brief GameAssetCore 核心加载与常驻保活热路径基准性能测试
 */

#include "Asset/FGameAssetLoader.h"
#include "Asset/FGameAssetPinner.h"
#include "Asset/FScopedPinnedAsset.h"
#include "Asset/GameAssetTypes.h"

#if WITH_AUTOMATION_TESTS

#include <algorithm>
#include <cmath>
#include <numeric>

#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 5;

constexpr int32 ShortCircuitIterations = 50000;
constexpr double ShortCircuitBudgetNsPerOp = 500.0;

constexpr int32 PinChurnIterations = 20000;
constexpr double PinChurnBudgetNsPerOp = 600.0;

constexpr int32 ScopedPinIterations = 20000;
constexpr double ScopedPinBudgetNsPerOp = 700.0;

constexpr int32 BatchScanIterations = 10000;
constexpr double BatchScanBudgetNsPerOp = 3000.0;

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

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetShortCircuitBenchmarkTest,
    "Game.Asset.Benchmark.ShortCircuitLoad",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 基准测试：验证已驻留内存软引用资产的异步短路回调吞吐性能与纳秒级预算
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetShortCircuitBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* DummyOwner = NewObject<UGameInstance>();
    UGameInstance* ResidentTarget = NewObject<UGameInstance>();
    const TSoftObjectPtr<UGameInstance> ResidentSoftPtr(ResidentTarget);

    int32 CallbackCounter = 0;

    const FBenchmarkMetrics Metrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ShortCircuitIterations,
        [&](int32 Iterations) {
            for (int32 i = 0; i < Iterations; ++i) {
                FGameAssetLoader::LoadAsync<UGameInstance>(
                    ResidentSoftPtr,
                    DummyOwner,
                    [&CallbackCounter](UGameInstance*) {
                        ++CallbackCounter;
                    }
                );
            }
        }
    );

    TestTrue(TEXT("短路高频触发回调已执行有效次数"), CallbackCounter > 0);
    TestTrue(
        FString::Printf(
            TEXT("已驻留资产短路每操作耗时 Median=%.2f ns/op 应低于预算 %.2f ns/op (Mean=%.2f, CV=%.2f%%, Ops/sec=%.0f)"),
            Metrics.MedianNs,
            ShortCircuitBudgetNsPerOp,
            Metrics.MeanNs,
            Metrics.CvPercent,
            Metrics.OpsPerSec
        ),
        Metrics.MedianNs <= ShortCircuitBudgetNsPerOp
    );

    DummyOwner->MarkAsGarbage();
    ResidentTarget->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetPinChurnBenchmarkTest,
    "Game.Asset.Benchmark.PinningChurn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 基准测试：验证 FGameAssetPinner 高频 Pin/Unpin 引用计数增减与互斥锁热路径性能
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetPinChurnBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* ResidentTarget = NewObject<UGameInstance>();
    const FSoftObjectPath ResidentPath(ResidentTarget);

    FGameAssetPinner::ClearAllPinnedAssets();
    FGameAssetPinner::PinAsset(ResidentPath);

    const FBenchmarkMetrics Metrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        PinChurnIterations,
        [&](int32 Iterations) {
            for (int32 i = 0; i < Iterations; ++i) {
                FGameAssetPinner::PinAsset(ResidentPath);
                FGameAssetPinner::UnpinAsset(ResidentPath);
            }
        }
    );

    TestTrue(TEXT("测试后基准锁定依然存续"), FGameAssetPinner::IsAssetPinned(ResidentPath));
    TestTrue(
        FString::Printf(
            TEXT("Pin/Unpin 高频操作耗时 Median=%.2f ns/op 应低于预算 %.2f ns/op (Mean=%.2f, CV=%.2f%%, Ops/sec=%.0f)"),
            Metrics.MedianNs,
            PinChurnBudgetNsPerOp,
            Metrics.MeanNs,
            Metrics.CvPercent,
            Metrics.OpsPerSec
        ),
        Metrics.MedianNs <= PinChurnBudgetNsPerOp
    );

    FGameAssetPinner::ClearAllPinnedAssets();
    ResidentTarget->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetScopedPinBenchmarkTest,
    "Game.Asset.Benchmark.ScopedPinRaii",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 基准测试：验证 FScopedPinnedAsset 的 RAII 栈构造、解引用访问与析构开销
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetScopedPinBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* ResidentTarget = NewObject<UGameInstance>();
    const TSoftObjectPtr<UGameInstance> ResidentSoftPtr(ResidentTarget);

    FGameAssetPinner::ClearAllPinnedAssets();
    FGameAssetPinner::PinAsset(ResidentSoftPtr.ToSoftObjectPath());

    int32 ResolvedCounter = 0;

    const FBenchmarkMetrics Metrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ScopedPinIterations,
        [&](int32 Iterations) {
            for (int32 i = 0; i < Iterations; ++i) {
                FScopedPinnedAsset ScopedPin(ResidentSoftPtr);
                if (UGameInstance* Instance = ScopedPin.Get<UGameInstance>()) {
                    ++ResolvedCounter;
                }
            }
        }
    );

    TestTrue(TEXT("RAII 循环中成功解析出对象"), ResolvedCounter > 0);
    TestTrue(
        FString::Printf(
            TEXT("ScopedPin 栈构造析构耗时 Median=%.2f ns/op 应低于预算 %.2f ns/op (Mean=%.2f, CV=%.2f%%, Ops/sec=%.0f)"),
            Metrics.MedianNs,
            ScopedPinBudgetNsPerOp,
            Metrics.MeanNs,
            Metrics.CvPercent,
            Metrics.OpsPerSec
        ),
        Metrics.MedianNs <= ScopedPinBudgetNsPerOp
    );

    FGameAssetPinner::ClearAllPinnedAssets();
    ResidentTarget->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetBatchScanBenchmarkTest,
    "Game.Asset.Benchmark.BatchPathScan",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 基准测试：验证 LoadBatchAsync 对多路径数组（16 路径）快速过滤与驻留短路探测性能
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetBatchScanBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* DummyOwner = NewObject<UGameInstance>();
    constexpr int32 TargetCount = 16;
    TArray<UGameInstance*> Targets;
    TArray<FSoftObjectPath> Paths;
    Targets.Reserve(TargetCount);
    Paths.Reserve(TargetCount);

    for (int32 i = 0; i < TargetCount; ++i) {
        UGameInstance* Target = NewObject<UGameInstance>();
        Targets.Add(Target);
        Paths.Add(FSoftObjectPath(Target));
    }

    int32 BatchDoneCounter = 0;

    const FBenchmarkMetrics Metrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        BatchScanIterations,
        [&](int32 Iterations) {
            for (int32 i = 0; i < Iterations; ++i) {
                FGameAssetLoader::LoadBatchAsync(
                    Paths,
                    DummyOwner,
                    [&BatchDoneCounter]() {
                        ++BatchDoneCounter;
                    }
                );
            }
        }
    );

    TestTrue(TEXT("批量短路快速完成回调已触发有效次数"), BatchDoneCounter > 0);
    TestTrue(
        FString::Printf(
            TEXT("16路径批量短路探测耗时 Median=%.2f ns/op 应低于预算 %.2f ns/op (Mean=%.2f, CV=%.2f%%, Ops/sec=%.0f)"),
            Metrics.MedianNs,
            BatchScanBudgetNsPerOp,
            Metrics.MeanNs,
            Metrics.CvPercent,
            Metrics.OpsPerSec
        ),
        Metrics.MedianNs <= BatchScanBudgetNsPerOp
    );

    DummyOwner->MarkAsGarbage();
    for (UGameInstance* Target : Targets) {
        Target->MarkAsGarbage();
    }
    return true;
}

#endif
