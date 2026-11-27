/**
 * @file FGlobalDilationMixer.cpp
 * @brief 全局分层时间膨胀混音器实现
 */

#include "Time/FGlobalDilationMixer.h"

namespace {
constexpr float MinGlobalDilationScale = 0.0001f;
constexpr float MaxGlobalDilationScale = 20.0f;
}

/**
 * @brief 在指定通道请求全局时间膨胀（重复请求整体覆盖该通道既有状态）
 * @param InChannel 目标通道
 * @param InDuration 持续时间（秒，<= 0 表示持久有效直至手动清除）
 * @param InTimeDilation 目标时间缩放（钳制到 [0.0001, 20]）
 * @param InBlendOutDuration 结束时的平滑淡出时间（秒，负数按 0 处理）
 */
void FGlobalDilationMixer::Request(ETimeDilationChannel InChannel, float InDuration, float InTimeDilation, float InBlendOutDuration) {
    uint32 Index = 0;
    if (!TryGetChannelIndex(InChannel, Index)) {
        return;
    }

    FGlobalChannelState& State = ChannelStates[Index];
    State.TargetDilation = FMath::Clamp(InTimeDilation, MinGlobalDilationScale, MaxGlobalDilationScale);
    State.CurrentDilation = State.TargetDilation;
    State.TotalDuration = FMath::Max(0.0f, InDuration);
    State.RemainingDuration = State.TotalDuration;
    State.BlendOutDuration = FMath::Max(0.0f, InBlendOutDuration);
    State.RemainingBlendOut = 0.0f;
    State.bActive = true;

    RecomputeEffectiveDilation();
}

/**
 * @brief 清除指定通道的全局时间膨胀
 * @param InChannel 目标通道
 */
void FGlobalDilationMixer::Clear(ETimeDilationChannel InChannel) {
    uint32 Index = 0;
    if (!TryGetChannelIndex(InChannel, Index) || !ChannelStates[Index].bActive) {
        return;
    }

    ChannelStates[Index].Reset();
    RecomputeEffectiveDilation();
}

/**
 * @brief 清除指定通道的全局时间膨胀并平滑淡出恢复
 * @param InChannel 目标通道
 * @param InBlendOutDuration 淡出过渡时间（秒，<= 0 时等价于立即清除）
 */
void FGlobalDilationMixer::ClearWithBlend(ETimeDilationChannel InChannel, float InBlendOutDuration) {
    uint32 Index = 0;
    if (!TryGetChannelIndex(InChannel, Index) || !ChannelStates[Index].bActive) {
        return;
    }

    if (!(InBlendOutDuration > 0.0f)) {
        Clear(InChannel);
        return;
    }

    FGlobalChannelState& State = ChannelStates[Index];
    State.TargetDilation = State.CurrentDilation;
    State.RemainingDuration = 0.0f;
    State.BlendOutDuration = InBlendOutDuration;
    State.RemainingBlendOut = InBlendOutDuration;
    RecomputeEffectiveDilation();
}

/**
 * @brief 清除所有通道的全局时间膨胀
 */
void FGlobalDilationMixer::ClearAll() {
    if (!bHasActiveChannel) {
        return;
    }

    for (FGlobalChannelState& State : ChannelStates) {
        State.Reset();
    }
    EffectiveDilation = 1.0f;
    bHasActiveChannel = false;
}

/**
 * @brief 检查指定通道是否处于激活状态
 * @param InChannel 目标通道
 * @return 激活返回 true
 */
bool FGlobalDilationMixer::IsActive(ETimeDilationChannel InChannel) const noexcept {
    uint32 Index = 0;
    return TryGetChannelIndex(InChannel, Index) && ChannelStates[Index].bActive;
}

/**
 * @brief 获取指定通道当前的时间缩放值
 * @param InChannel 目标通道
 * @return 通道时间缩放值（未激活返回 1.0）
 */
float FGlobalDilationMixer::GetChannelDilation(ETimeDilationChannel InChannel) const noexcept {
    uint32 Index = 0;
    if (TryGetChannelIndex(InChannel, Index) && ChannelStates[Index].bActive) {
        return ChannelStates[Index].CurrentDilation;
    }
    return 1.0f;
}

/**
 * @brief 获取当前生效的最终全局时间缩放值
 * @return 生效时间缩放
 */
float FGlobalDilationMixer::GetEffectiveDilation() const noexcept {
    return EffectiveDilation;
}

/**
 * @brief 推进计时与淡出插值，单帧余量在当前通道内连续消费持续与淡出阶段（InRealDeltaSeconds <= 0 时忽略）
 * @param InRealDeltaSeconds 真实流逝秒数
 */
void FGlobalDilationMixer::Advance(float InRealDeltaSeconds) {
    if (!(InRealDeltaSeconds > 0.0f) || !bHasActiveChannel) {
        return;
    }

    bool bChannelsChanged = false;
    for (FGlobalChannelState& State : ChannelStates) {
        if (State.bActive && AdvanceChannel(State, InRealDeltaSeconds)) {
            bChannelsChanged = true;
        }
    }

    if (bChannelsChanged) {
        RecomputeEffectiveDilation();
    }
}

/**
 * @brief 校验并转换通道枚举为静态数组合法索引
 * @param InChannel 目标通道
 * @param OutIndex 输出索引
 * @return 索引合法返回 true
 */
bool FGlobalDilationMixer::TryGetChannelIndex(ETimeDilationChannel InChannel, uint32& OutIndex) noexcept {
    const uint32 RawIndex = static_cast<uint32>(InChannel);
    if (RawIndex >= ChannelCount) {
        return false;
    }
    OutIndex = RawIndex;
    return true;
}

/**
 * @brief 推进单个激活通道的持续与淡出阶段
 * @param State 目标通道状态引用
 * @param InRealDeltaSeconds 真实流逝秒数
 * @return 通道生效缩放或激活态发生变化返回 true
 */
bool FGlobalDilationMixer::AdvanceChannel(FGlobalChannelState& State, float InRealDeltaSeconds) {
    float RemainingDelta = InRealDeltaSeconds;
    if (State.RemainingDuration > 0.0f) {
        State.RemainingDuration -= RemainingDelta;
        if (State.RemainingDuration > 0.0f) {
            return false;
        }
        RemainingDelta = -State.RemainingDuration;
        State.RemainingDuration = 0.0f;
        if (State.BlendOutDuration <= 0.0f) {
            State.Reset();
            return true;
        }
        State.RemainingBlendOut = State.BlendOutDuration;
    }

    if (State.RemainingBlendOut <= 0.0f) {
        return false;
    }

    State.RemainingBlendOut -= RemainingDelta;
    if (State.RemainingBlendOut <= 0.0f) {
        State.Reset();
        return true;
    }

    const float Alpha = FMath::Clamp(1.0f - (State.RemainingBlendOut / State.BlendOutDuration), 0.0f, 1.0f);
    State.CurrentDilation = FMath::Lerp(State.TargetDilation, 1.0f, Alpha);
    return true;
}

/**
 * @brief 依据通道优先级重算生效时间缩放与整体活跃标记
 */
void FGlobalDilationMixer::RecomputeEffectiveDilation() noexcept {
    for (const FGlobalChannelState& State : ChannelStates) {
        if (State.bActive) {
            EffectiveDilation = State.CurrentDilation;
            bHasActiveChannel = true;
            return;
        }
    }
    EffectiveDilation = 1.0f;
    bHasActiveChannel = false;
}
