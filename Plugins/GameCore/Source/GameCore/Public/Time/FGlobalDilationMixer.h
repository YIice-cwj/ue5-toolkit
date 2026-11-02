/**
 * @file FGlobalDilationMixer.h
 * @brief 全局分层时间膨胀混音器（纯 C++ · 通道优先级覆盖与平滑淡出）
 */

#pragma once

#include "CoreMinimal.h"
#include "Time/TimeDilationTypes.h"

/**
 * @brief 全局分层时间膨胀混音器
 */
struct GAMECORE_API FGlobalDilationMixer {
    /**
     * @brief 构造：按通道枚举数量预分配通道状态数组
     */
    FGlobalDilationMixer();

    /**
     * @brief 在指定通道请求全局时间膨胀（重复请求整体覆盖该通道既有状态）
     * @param InChannel 目标通道
     * @param InDuration 持续时间（秒，<= 0 表示持久有效直至手动清除）
     * @param InTimeDilation 目标时间缩放（钳制到 [0, 20]）
     * @param InBlendOutDuration 结束时的平滑淡出时间（秒，负数按 0 处理）
     */
    void Request(ETimeDilationChannel InChannel, float InDuration, float InTimeDilation, float InBlendOutDuration);

    /**
     * @brief 清除指定通道的全局时间膨胀
     * @param InChannel 目标通道
     */
    void Clear(ETimeDilationChannel InChannel);

    /**
     * @brief 清除指定通道的全局时间膨胀并平滑淡出恢复
     * @param InChannel 目标通道
     * @param InBlendOutDuration 淡出过渡时间（秒，<= 0 时等价于立即清除）
     */
    void ClearWithBlend(ETimeDilationChannel InChannel, float InBlendOutDuration);

    /**
     * @brief 清除所有通道的全局时间膨胀
     */
    void ClearAll();

    /**
     * @brief 检查指定通道是否处于激活状态
     * @param InChannel 目标通道
     * @return 激活返回 true
     */
    [[nodiscard]] bool IsActive(ETimeDilationChannel InChannel) const;

    /**
     * @brief 获取指定通道当前的时间缩放值
     * @param InChannel 目标通道
     * @return 通道时间缩放值（未激活返回 1.0）
     */
    [[nodiscard]] float GetChannelDilation(ETimeDilationChannel InChannel) const;

    /**
     * @brief 获取当前生效的最终全局时间缩放值
     * @return 生效时间缩放
     */
    [[nodiscard]] float GetEffectiveDilation() const;

    /**
     * @brief 推进计时与淡出插值，单帧余量在当前通道内连续消费持续与淡出阶段
     * @param InRealDeltaSeconds 真实流逝秒数
     */
    void Advance(float InRealDeltaSeconds);

private:
    /**
     * @brief 依据通道优先级重算生效时间缩放
     *
     * 实现思路：通道按枚举顺序即优先级（数值越小优先级越高），
     * 取首个激活通道的当前缩放作为生效值，高优先级完全覆盖低优先级
     */
    void RecomputeEffectiveDilation();

private:
    TArray<FGlobalChannelState> ChannelStates;
    float EffectiveDilation = 1.0f;
};
