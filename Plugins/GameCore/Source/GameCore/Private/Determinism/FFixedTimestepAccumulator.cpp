/**
 * @file FFixedTimestepAccumulator.cpp
 * @brief FFixedTimestepAccumulator 固定时间步长累加器实现
 */

#include "Determinism/FFixedTimestepAccumulator.h"

/**
 * @brief 构造函数：初始化步长与保护阈值
 * @param InFixedDeltaTime 固定子步步长（秒，默认 1/60s）
 * @param InMaxSubsteps 单帧允许执行的最大子步数（默认 4）
 * @param InMaxDeltaTime 单帧接收的最大流逝时间保护截断（秒，默认 0.25s）
 */
FFixedTimestepAccumulator::FFixedTimestepAccumulator(
    double InFixedDeltaTime,
    int32 InMaxSubsteps,
    double InMaxDeltaTime
)
    : FixedDeltaTime(InFixedDeltaTime > 0.0 ? InFixedDeltaTime : (1.0 / 60.0))
    , MaxSubsteps(FMath::Max(1, InMaxSubsteps))
    , ConfiguredMaxDeltaTime(InMaxDeltaTime > 0.0 ? InMaxDeltaTime : 0.25)
    , MaxDeltaTime(FMath::Max(FixedDeltaTime, ConfiguredMaxDeltaTime))
    , AccumulatedTime(0.0) {
}

/**
 * @brief 推进真实时间并按固定步长离散触发回调
 * @param InDeltaSeconds 本帧真实流逝秒数
 * @param SubstepCallback 固定子步回调（入参为 FixedDeltaTime）
 * @return 本次推进实际执行的子步次数；当单帧触达 MaxSubsteps 上限时，超出整步的积压时间将被模除丢弃以防螺旋死锁
 */
int32 FFixedTimestepAccumulator::Advance(
    double InDeltaSeconds,
    const TFunctionRef<void(double FixedDt)>& SubstepCallback
) {
    if (InDeltaSeconds <= 0.0) {
        return 0;
    }

    const double ClampedDelta = FMath::Clamp(InDeltaSeconds, 0.0, MaxDeltaTime);
    AccumulatedTime += ClampedDelta;

    int32 ExecutedSubsteps = 0;
    while (AccumulatedTime >= FixedDeltaTime && ExecutedSubsteps < MaxSubsteps) {
        SubstepCallback(FixedDeltaTime);
        AccumulatedTime -= FixedDeltaTime;
        ++ExecutedSubsteps;
    }

    if (AccumulatedTime >= FixedDeltaTime) {
        AccumulatedTime = FMath::Fmod(AccumulatedTime, FixedDeltaTime);
    }

    return ExecutedSubsteps;
}

/**
 * @brief 获取渲染状态插值因子（Alpha）
 * @return [0.0, 1.0] 范围内的未消费时间比例，用于渲染时在上一逻辑步与当前逻辑步之间插值
 */
double FFixedTimestepAccumulator::GetInterpolationAlpha() const noexcept {
    if (FixedDeltaTime <= 0.0) {
        return 0.0;
    }
    return FMath::Clamp(AccumulatedTime / FixedDeltaTime, 0.0, 1.0);
}

/**
 * @brief 获取当前固定步长（秒）
 * @return 固定步长
 */
double FFixedTimestepAccumulator::GetFixedDeltaTime() const noexcept {
    return FixedDeltaTime;
}

/**
 * @brief 动态调整固定步长（不会永久污染用户配置的基准最大帧时间）
 * @param InFixedDeltaTime 新固定步长（必须大于 0）
 */
void FFixedTimestepAccumulator::SetFixedDeltaTime(double InFixedDeltaTime) {
    if (InFixedDeltaTime > 0.0) {
        FixedDeltaTime = InFixedDeltaTime;
        MaxDeltaTime = FMath::Max(FixedDeltaTime, ConfiguredMaxDeltaTime);
    }
}

/**
 * @brief 获取当前单帧最大允许子步数
 * @return 最大子步数
 */
int32 FFixedTimestepAccumulator::GetMaxSubsteps() const noexcept {
    return MaxSubsteps;
}

/**
 * @brief 设置单帧最大允许子步数
 * @param InMaxSubsteps 最大子步数（必须大于等于 1）
 */
void FFixedTimestepAccumulator::SetMaxSubsteps(int32 InMaxSubsteps) {
    MaxSubsteps = FMath::Max(1, InMaxSubsteps);
}

/**
 * @brief 获取当前未消费的累计流逝时间（秒）
 * @return 累计秒数
 */
double FFixedTimestepAccumulator::GetAccumulatedTime() const noexcept {
    return AccumulatedTime;
}

/**
 * @brief 重置累加器（清空未消费余量）
 */
void FFixedTimestepAccumulator::Reset() noexcept {
    AccumulatedTime = 0.0;
}
