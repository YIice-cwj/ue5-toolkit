/**
 * @file FGlobalDilationMixer.h
 * @brief 全局分层时间膨胀混音器（纯 C++ · 静态零堆分配 · 通道优先级覆盖与平滑淡出）
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "Time/TimeDilationTypes.h"

/**
 * @brief 全局分层时间膨胀混音器
 */
struct GAMEFEEDBACK_API FGlobalDilationMixer {
    static constexpr uint32 ChannelCount = static_cast<uint32>(ETimeDilationChannel::Count);

    /**
     * @brief 默认构造：静态内联初始化全部通道状态（零堆分配）
     */
    FGlobalDilationMixer() = default;

    /**
     * @brief 在指定通道请求全局时间膨胀（重复请求整体覆盖该通道既有状态）
     * @param InChannel 目标通道
     * @param InDuration 持续时间（秒，<= 0 表示持久有效直至手动清除）
     * @param InTimeDilation 目标时间缩放（钳制到 [0.0001, 20]）
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
    [[nodiscard]] bool IsActive(ETimeDilationChannel InChannel) const noexcept;

    /**
     * @brief 检查是否存在任意处于激活状态的全局时间膨胀通道
     * @return 存在激活通道返回 true
     */
    [[nodiscard]] bool HasActive() const noexcept {
        return bHasActiveChannel;
    }

    /**
     * @brief 获取指定通道当前的时间缩放值
     * @param InChannel 目标通道
     * @return 通道时间缩放值（未激活返回 1.0）
     */
    [[nodiscard]] float GetChannelDilation(ETimeDilationChannel InChannel) const noexcept;

    /**
     * @brief 获取当前生效的最终全局时间缩放值
     * @return 生效时间缩放
     */
    [[nodiscard]] float GetEffectiveDilation() const noexcept;

    /**
     * @brief 推进计时与淡出插值，单帧余量在当前通道内连续消费持续与淡出阶段（InRealDeltaSeconds <= 0 时忽略）
     * @param InRealDeltaSeconds 真实流逝秒数
     */
    void Advance(float InRealDeltaSeconds);

private:
    /**
     * @brief 校验并转换通道枚举为静态数组合法索引
     * @param InChannel 目标通道
     * @param OutIndex 输出索引
     * @return 索引合法返回 true
     */
    [[nodiscard]] static bool TryGetChannelIndex(ETimeDilationChannel InChannel, uint32& OutIndex) noexcept;

    /**
     * @brief 推进单个激活通道的持续与淡出阶段
     * @param State 目标通道状态引用
     * @param InRealDeltaSeconds 真实流逝秒数
     * @return 通道生效缩放或激活态发生变化返回 true
     */
    static bool AdvanceChannel(FGlobalChannelState& State, float InRealDeltaSeconds);

    /**
     * @brief 依据通道优先级重算生效时间缩放与整体活跃标记
     */
    void RecomputeEffectiveDilation() noexcept;

    TStaticArray<FGlobalChannelState, ChannelCount> ChannelStates{InPlace};
    float EffectiveDilation = 1.0f;
    bool bHasActiveChannel = false;
};
