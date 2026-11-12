/**
 * @file FFixedTimestepAccumulator.h
 * @brief 固定时间步长累加器（防螺旋死锁截断与渲染插值 Alpha 平滑计算）
 */

#pragma once

#include "CoreMinimal.h"

/**
 * @brief 纯数学固定时间步长累加推进器
 *
 * 用于网络快照预测、物理子步更新与技能判定等要求严格确定性的固定更新循环：
 * 1. 与渲染帧率解耦，单帧累加 DeltaTime 并按固定步长（如 60Hz = 0.016667s）离散执行子步；
 * 2. 内建 MaxSubsteps 防螺旋死锁（Spiral of Death）保护，避免掉帧时子步堆积雪崩；
 * 3. 输出 [0.0, 1.0) 范围的渲染插值因子 Alpha（用于前后物理帧平滑插值消除抖动）。
 */
class GAMECORE_API FFixedTimestepAccumulator {
public:
    /**
     * @brief 构造函数：初始化步长与保护阈值
     * @param InFixedDeltaTime 固定子步步长（秒，默认 1/60s）
     * @param InMaxSubsteps 单帧允许执行的最大子步数（默认 4）
     * @param InMaxDeltaTime 单帧接收的最大流逝时间保护截断（秒，默认 0.25s）
     */
    explicit FFixedTimestepAccumulator(
        double InFixedDeltaTime = 1.0 / 60.0,
        int32 InMaxSubsteps = 4,
        double InMaxDeltaTime = 0.25
    );

    /**
     * @brief 推进真实时间并按固定步长离散触发回调
     * @param InDeltaSeconds 本帧真实流逝秒数
     * @param SubstepCallback 固定子步回调（入参为 FixedDeltaTime）
     * @return 本次推进实际执行的子步次数；当单帧触达 MaxSubsteps 上限时，超出整步的积压时间将被模除丢弃以防螺旋死锁
     */
    int32 Advance(double InDeltaSeconds, const TFunctionRef<void(double FixedDt)>& SubstepCallback);

    /**
     * @brief 获取渲染状态插值因子（Alpha）
     * @return [0.0, 1.0] 范围内的未消费时间比例，用于渲染时在上一逻辑步与当前逻辑步之间插值
     */
    [[nodiscard]] double GetInterpolationAlpha() const noexcept;

    /**
     * @brief 获取当前固定步长（秒）
     * @return 固定步长
     */
    [[nodiscard]] double GetFixedDeltaTime() const noexcept;

    /**
     * @brief 动态调整固定步长（不会永久污染用户配置的基准最大帧时间）
     * @param InFixedDeltaTime 新固定步长（必须大于 0）
     */
    void SetFixedDeltaTime(double InFixedDeltaTime);

    /**
     * @brief 获取当前单帧最大允许子步数
     * @return 最大子步数
     */
    [[nodiscard]] int32 GetMaxSubsteps() const noexcept;

    /**
     * @brief 设置单帧最大允许子步数
     * @param InMaxSubsteps 最大子步数（必须大于等于 1）
     */
    void SetMaxSubsteps(int32 InMaxSubsteps);

    /**
     * @brief 检查当前未消费的累计时间是否满足至少触发一次固定子步
     * @return 存在待执行子步返回 true
     */
    [[nodiscard]] bool HasPendingSubstep() const noexcept;

    /**
     * @brief 获取当前未消费的累计流逝时间（秒）
     * @return 累计秒数
     */
    [[nodiscard]] double GetAccumulatedTime() const noexcept;

    /**
     * @brief 重置累加器（清空未消费余量）
     */
    void Reset() noexcept;

private:
    double FixedDeltaTime;
    int32 MaxSubsteps;
    double ConfiguredMaxDeltaTime;
    double MaxDeltaTime;
    double AccumulatedTime;
};
