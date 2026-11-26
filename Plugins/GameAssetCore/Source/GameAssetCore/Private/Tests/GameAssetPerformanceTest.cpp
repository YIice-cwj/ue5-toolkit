/**
 * @file GameAssetPerformanceTest.cpp
 * @brief GameAssetCore 全组件热路径微基准性能测试套件（预热 + 7 轮采样统计中位数、均值、极值、CV% 与吞吐量）
 */

#include "Blueprint/UGameAssetBlueprintLibrary.h"
#include "Loader/FGameAssetLoader.h"
#include "Pinning/FGameAssetPinner.h"
#include "Pinning/FScopedPinnedAsset.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"
#include "Logging/GameLogCategories.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/AutomationTest.h"

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 7;

constexpr int32 ShortCircuitIterations = 50000;
constexpr double ShortCircuitBudgetNsPerOp = 500.0;

constexpr int32 CoroFastPathIterations = 20000;
constexpr double CoroFastPathBudgetNsPerOp = 1500.0;

constexpr int32 PinChurnIterations = 25000;
constexpr double PinChurnBudgetNsPerOp = 600.0;

constexpr int32 ScopedPinIterations = 25000;
constexpr double ScopedPinBudgetNsPerOp = 600.0;

constexpr int32 BatchScanIterations = 10000;
constexpr double BatchScanBudgetNsPerOp = 3000.0;

constexpr int32 BlueprintQueryIterations = 100000;
constexpr double BlueprintQueryBudgetNsPerOp = 150.0;

struct FAssetBenchmarkMetrics {
    double MedianNs = 0.0;
    double MeanNs = 0.0;
    double CvPercent = 0.0;
    double MinNs = 0.0;
    double MaxNs = 0.0;
    double OpsPerSec = 0.0;
};

/**
 * @brief 执行多轮预热与采样微基准，计算中位数、均值、极值与变异系数 (CV%)
 * @tparam TFunc 待测闭包类型
 * @param WarmupRounds 预热丢弃轮数
 * @param SampleRounds 正式采样轮数（>= 5 轮）
 * @param IterationsPerRound 每轮执行迭代次数
 * @param Body 待测循环体闭包
 * @return 统计指标结果
 */
template <typename TFunc>
FAssetBenchmarkMetrics RunAssetBenchmark(
    const int32 WarmupRounds,
    const int32 SampleRounds,
    const int32 IterationsPerRound,
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
    for (const double Sample : SamplesNs) {
        const double Diff = Sample - MeanNs;
        VarianceSum += Diff * Diff;
    }
    const double StdDev = FMath::Sqrt(VarianceSum / static_cast<double>(SampleRounds));
    const double CvPercent = (MeanNs > 0.0) ? ((StdDev / MeanNs) * 100.0) : 0.0;

    FAssetBenchmarkMetrics Metrics;
    Metrics.MedianNs = MedianNs;
    Metrics.MeanNs = MeanNs;
    Metrics.CvPercent = CvPercent;
    Metrics.MinNs = SamplesNs[0];
    Metrics.MaxNs = SamplesNs.Last();
    Metrics.OpsPerSec = (MedianNs > 0.0) ? (1.0e9 / MedianNs) : 0.0;
    return Metrics;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetShortCircuitPerfTest,
    "Game.Asset.Perf.ShortCircuitLoad",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准：已驻留内存软引用资产的 LoadAsync 同步短路回调吞吐
 * @param Parameters 自动化测试参数
 * @return 落在预算内返回 true
 */
bool FGameAssetShortCircuitPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* DummyOwner = NewObject<UGameInstance>();
    UGameInstance* ResidentTarget = NewObject<UGameInstance>();
    const TSoftObjectPtr<UGameInstance> ResidentSoftPtr(ResidentTarget);

    volatile uintptr_t Sink = 0;

    const FAssetBenchmarkMetrics Metrics = RunAssetBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ShortCircuitIterations,
        [&ResidentSoftPtr, DummyOwner, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                FGameAssetLoader::LoadAsync<UGameInstance>(
                    ResidentSoftPtr,
                    DummyOwner,
                    [&Sink](UGameInstance* Loaded) {
                        Sink = Sink + reinterpret_cast<uintptr_t>(Loaded);
                    }
                );
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("LoadAsync 驻留短路 Median=%.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, ShortCircuitBudgetNsPerOp),
        Metrics.MedianNs <= ShortCircuitBudgetNsPerOp
    );

    const FString Report = FString::Printf(
        TEXT("[GameAssetPerf] LoadAsync ShortCircuit: %.2f ns/op (Mean: %.2f, CV: %.2f%%, %.2f Mops/s)"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    );
    GAME_LOG(Display, TEXT("%s"), *Report);
    AddInfo(Report);

    DummyOwner->MarkAsGarbage();
    ResidentTarget->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetCoroResidentFastPathPerfTest,
    "Game.Asset.Perf.CoroResidentFastPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准：事件驱动 LoadAsyncCoro 在资产已驻留时的零流送挂起直返吞吐
 * @param Parameters 自动化测试参数
 * @return 落在预算内返回 true
 */
bool FGameAssetCoroResidentFastPathPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* DummyOwner = NewObject<UGameInstance>();
    UGameInstance* ResidentTarget = NewObject<UGameInstance>();
    const TSoftObjectPtr<UGameInstance> ResidentSoftPtr(ResidentTarget);

    volatile uintptr_t Sink = 0;

    const FAssetBenchmarkMetrics Metrics = RunAssetBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        CoroFastPathIterations,
        [&ResidentSoftPtr, DummyOwner, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                TGameCoroTask<UGameInstance*> Task = FGameAssetLoader::LoadAsyncCoro<UGameInstance>(ResidentSoftPtr, DummyOwner);
                Sink = Sink + reinterpret_cast<uintptr_t>(Task.GetResult().GetValue());
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("LoadAsyncCoro 驻留快路径 Median=%.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, CoroFastPathBudgetNsPerOp),
        Metrics.MedianNs <= CoroFastPathBudgetNsPerOp
    );

    const FString Report = FString::Printf(
        TEXT("[GameAssetPerf] LoadAsyncCoro Resident FastPath: %.2f ns/op (Mean: %.2f, CV: %.2f%%, %.2f Mops/s)"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    );
    GAME_LOG(Display, TEXT("%s"), *Report);
    AddInfo(Report);

    DummyOwner->MarkAsGarbage();
    ResidentTarget->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetPinChurnAndLookupPerfTest,
    "Game.Asset.Perf.PinningChurnAndLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准：FGameAssetPinner 高频 Pin/Unpin 引用计数热路径与 O(1) GetPinnedAsset 缓存指针读取吞吐
 * @param Parameters 自动化测试参数
 * @return 落在预算内返回 true
 */
bool FGameAssetPinChurnAndLookupPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* ResidentTarget = NewObject<UGameInstance>();
    const FSoftObjectPath ResidentPath(ResidentTarget);

    FGameAssetPinner::ClearAllPinnedAssets();
    FGameAssetPinner::Startup();
    (void)FGameAssetPinner::PinAsset(ResidentPath);

    volatile uintptr_t Sink = 0;

    const FAssetBenchmarkMetrics ChurnMetrics = RunAssetBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        PinChurnIterations,
        [&ResidentPath, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const TGameStatusResult<UObject*> PinRes = FGameAssetPinner::PinAsset(ResidentPath);
                Sink = Sink + reinterpret_cast<uintptr_t>(PinRes.GetValue());
                (void)FGameAssetPinner::UnpinAsset(ResidentPath);
            }
        }
    );

    const FAssetBenchmarkMetrics LookupMetrics = RunAssetBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        PinChurnIterations,
        [&ResidentPath, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Sink = Sink + reinterpret_cast<uintptr_t>(FGameAssetPinner::GetPinnedAsset(ResidentPath));
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("Pin/Unpin 热路径 Median=%.2f ns/op 应低于预算 %.0f ns/op"), ChurnMetrics.MedianNs, PinChurnBudgetNsPerOp),
        ChurnMetrics.MedianNs <= PinChurnBudgetNsPerOp
    );

    const FString Report = FString::Printf(
        TEXT("[GameAssetPerf] Pin+Unpin Churn: %.2f ns/op (CV: %.2f%%, %.2f Mops/s) | GetPinnedAsset Lookup: %.2f ns/op (CV: %.2f%%, %.2f Mops/s)"),
        ChurnMetrics.MedianNs,
        ChurnMetrics.CvPercent,
        ChurnMetrics.OpsPerSec / 1.0e6,
        LookupMetrics.MedianNs,
        LookupMetrics.CvPercent,
        LookupMetrics.OpsPerSec / 1.0e6
    );
    GAME_LOG(Display, TEXT("%s"), *Report);
    AddInfo(Report);

    FGameAssetPinner::ClearAllPinnedAssets();
    ResidentTarget->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetScopedPinPerfTest,
    "Game.Asset.Perf.ScopedPinRaiiVsLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准：FScopedPinnedAsset 单次加锁构造/析构开销与栈缓存 Get<T>() 零锁解引用对比
 * @param Parameters 自动化测试参数
 * @return 落在预算内返回 true
 */
bool FGameAssetScopedPinPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* ResidentTarget = NewObject<UGameInstance>();
    const TSoftObjectPtr<UGameInstance> ResidentSoftPtr(ResidentTarget);

    FGameAssetPinner::ClearAllPinnedAssets();
    FGameAssetPinner::Startup();
    (void)FGameAssetPinner::PinAsset(ResidentSoftPtr.ToSoftObjectPath());

    volatile uintptr_t Sink = 0;

    const FAssetBenchmarkMetrics ScopedMetrics = RunAssetBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ScopedPinIterations,
        [&ResidentSoftPtr, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                FScopedPinnedAsset ScopedPin(ResidentSoftPtr);
                Sink = Sink + reinterpret_cast<uintptr_t>(ScopedPin.Get<UGameInstance>());
            }
        }
    );

    const FScopedPinnedAsset HeldPin(ResidentSoftPtr);
    const FAssetBenchmarkMetrics CachedReadMetrics = RunAssetBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        BlueprintQueryIterations,
        [&HeldPin, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Sink = Sink + reinterpret_cast<uintptr_t>(HeldPin.Get<UGameInstance>());
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("FScopedPinnedAsset 栈构造析构 Median=%.2f ns/op 应低于预算 %.0f ns/op"), ScopedMetrics.MedianNs, ScopedPinBudgetNsPerOp),
        ScopedMetrics.MedianNs <= ScopedPinBudgetNsPerOp
    );

    const FString Report = FString::Printf(
        TEXT("[GameAssetPerf] FScopedPinnedAsset RAII Scope: %.2f ns/op (CV: %.2f%%, %.2f Mops/s) | Cached Get<T>(): %.2f ns/op (%.2f Mops/s)"),
        ScopedMetrics.MedianNs,
        ScopedMetrics.CvPercent,
        ScopedMetrics.OpsPerSec / 1.0e6,
        CachedReadMetrics.MedianNs,
        CachedReadMetrics.OpsPerSec / 1.0e6
    );
    GAME_LOG(Display, TEXT("%s"), *Report);
    AddInfo(Report);

    FGameAssetPinner::ClearAllPinnedAssets();
    ResidentTarget->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetBatchScanPerfTest,
    "Game.Asset.Perf.BatchDeduplicationAndShortCircuit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准：LoadBatchAsync 16 路径零分配驻留短路扫描吞吐
 * @param Parameters 自动化测试参数
 * @return 落在预算内返回 true
 */
bool FGameAssetBatchScanPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* DummyOwner = NewObject<UGameInstance>();
    constexpr int32 TargetCount = 16;
    TArray<UGameInstance*> Targets;
    TArray<FSoftObjectPath> Paths;
    Targets.Reserve(TargetCount);
    Paths.Reserve(TargetCount);

    for (int32 Index = 0; Index < TargetCount; ++Index) {
        UGameInstance* Target = NewObject<UGameInstance>();
        Targets.Add(Target);
        Paths.Add(FSoftObjectPath(Target));
    }

    volatile int32 BatchDoneSink = 0;

    const FAssetBenchmarkMetrics Metrics = RunAssetBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        BatchScanIterations,
        [&Paths, DummyOwner, &BatchDoneSink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                FGameAssetLoader::LoadBatchAsync(
                    Paths,
                    DummyOwner,
                    [&BatchDoneSink]() {
                        BatchDoneSink = BatchDoneSink + 1;
                    }
                );
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("16 路径批量驻留短路扫描 Median=%.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, BatchScanBudgetNsPerOp),
        Metrics.MedianNs <= BatchScanBudgetNsPerOp
    );

    const FString Report = FString::Printf(
        TEXT("[GameAssetPerf] Batch 16-Path ShortCircuit: %.2f ns/op (Mean: %.2f, CV: %.2f%%, %.2f Kops/s)"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e3
    );
    GAME_LOG(Display, TEXT("%s"), *Report);
    AddInfo(Report);

    DummyOwner->MarkAsGarbage();
    for (UGameInstance* Target : Targets) {
        Target->MarkAsGarbage();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetBlueprintQueryPerfTest,
    "Game.Asset.Perf.BlueprintQueryFastPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准：UGameAssetBlueprintLibrary::GetLoadedAsset / IsAssetLoaded 单次 TSoftObjectPtr::Get() 热路径吞吐
 * @param Parameters 自动化测试参数
 * @return 落在预算内返回 true
 */
bool FGameAssetBlueprintQueryPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* ResidentTarget = NewObject<UGameInstance>();
    const TSoftObjectPtr<UObject> ResidentSoft(ResidentTarget);

    volatile uintptr_t Sink = 0;

    const FAssetBenchmarkMetrics Metrics = RunAssetBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        BlueprintQueryIterations,
        [&ResidentSoft, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                if (UGameAssetBlueprintLibrary::IsAssetLoaded(ResidentSoft)) {
                    Sink = Sink + reinterpret_cast<uintptr_t>(UGameAssetBlueprintLibrary::GetLoadedAsset(ResidentSoft));
                }
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("蓝图驻留查询 Median=%.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, BlueprintQueryBudgetNsPerOp),
        Metrics.MedianNs <= BlueprintQueryBudgetNsPerOp
    );

    const FString Report = FString::Printf(
        TEXT("[GameAssetPerf] Blueprint IsAssetLoaded+GetLoadedAsset: %.2f ns/op (Mean: %.2f, CV: %.2f%%, %.2f Mops/s)"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    );
    GAME_LOG(Display, TEXT("%s"), *Report);
    AddInfo(Report);

    ResidentTarget->MarkAsGarbage();
    return true;
}

#endif
