/**
 * @file GameDataBenchmarkUtils.h
 * @brief GameData 系列微基准测试共享度量工具（预热、多轮采样、中位数与变异系数统计）
 */

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "Math/UnrealMathUtility.h"

namespace GameDataBenchmark {

inline constexpr int32 DefaultWarmupRounds = 2;
inline constexpr int32 DefaultSampleRounds = 5;

/**
 * @brief 单项微基准统计度量结果
 */
struct FBenchmarkMetrics {
    /**
     * @brief 中位数单次耗时（纳秒）
     */
    double MedianNs = 0.0;

    /**
     * @brief 算术平均单次耗时（纳秒）
     */
    double MeanNs = 0.0;

    /**
     * @brief 采样离散度变异系数（百分比 CV%）
     */
    double CvPercent = 0.0;

    /**
     * @brief 最小单次耗时（纳秒）
     */
    double MinNs = 0.0;

    /**
     * @brief 最大单次耗时（纳秒）
     */
    double MaxNs = 0.0;

    /**
     * @brief 按中位数推算的吞吐率（次/秒）
     */
    double OpsPerSec = 0.0;
};

/**
 * @brief 执行多轮基准测试，消除预热偏差并统计中位数与离散度 (CV%)
 * @tparam TFunc 待测闭包类型（签名 void(int32 Iterations)）
 * @param WarmupRounds 预热轮数（测量后丢弃）
 * @param SampleRounds 正式采样轮数（建议 >= 5 轮）
 * @param IterationsPerRound 每轮迭代执行次数
 * @param Body 待执行测量闭包
 * @return 包含中位数、均值、极值与离散度的统计度量
 */
template <typename TFunc>
[[nodiscard]] FBenchmarkMetrics RunBenchmark(
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

    FBenchmarkMetrics Metrics;
    Metrics.MedianNs = MedianNs;
    Metrics.MeanNs = MeanNs;
    Metrics.CvPercent = CvPercent;
    Metrics.MinNs = SamplesNs[0];
    Metrics.MaxNs = SamplesNs[SampleRounds - 1];
    Metrics.OpsPerSec = (MedianNs > 0.0) ? (1.0e9 / MedianNs) : 0.0;
    return Metrics;
}

}  // namespace GameDataBenchmark
