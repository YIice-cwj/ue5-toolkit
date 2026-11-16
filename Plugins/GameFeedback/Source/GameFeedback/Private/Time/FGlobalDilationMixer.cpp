/**
 * @file FGlobalDilationMixer.cpp
 * @brief 全局分层时间膨胀混音器实现
 */

#include "Time/FGlobalDilationMixer.h"

/**
 * @brief 构造：按通道枚举数量预分配通道状态数组
 */
FGlobalDilationMixer::FGlobalDilationMixer()
{
    ChannelStates.SetNum(static_cast<int32>(ETimeDilationChannel::Count));
}

/**
 * @brief 在指定通道请求全局时间膨胀（重复请求整体覆盖该通道既有状态）
 * @param InChannel 目标通道
 * @param InDuration 持续时间（秒，<= 0 表示持久有效直至手动清除）
 * @param InTimeDilation 目标时间缩放（钳制到 [0.0001, 20]）
 * @param InBlendOutDuration 结束时的平滑淡出时间（秒，负数按 0 处理）
 */
void FGlobalDilationMixer::Request(ETimeDilationChannel InChannel, float InDuration, float InTimeDilation, float InBlendOutDuration)
{
    const int32 Index = static_cast<int32>(InChannel);
    if (!ChannelStates.IsValidIndex(Index)) {
        return;
    }

    FGlobalChannelState& State = ChannelStates[Index];
    State.TargetDilation = FMath::Clamp(InTimeDilation, 0.0001f, 20.0f);
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
void FGlobalDilationMixer::Clear(ETimeDilationChannel InChannel)
{
    const int32 Index = static_cast<int32>(InChannel);
    if (!ChannelStates.IsValidIndex(Index) || !ChannelStates[Index].bActive) {
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
void FGlobalDilationMixer::ClearWithBlend(ETimeDilationChannel InChannel, float InBlendOutDuration)
{
    const int32 Index = static_cast<int32>(InChannel);
    if (!ChannelStates.IsValidIndex(Index) || !ChannelStates[Index].bActive) {
        return;
    }

    if (InBlendOutDuration <= 0.0f) {
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
void FGlobalDilationMixer::ClearAll()
{
    bool bNeedsUpdate = false;
    for (FGlobalChannelState& State : ChannelStates) {
        if (State.bActive) {
            State.Reset();
            bNeedsUpdate = true;
        }
    }
    if (bNeedsUpdate) {
        RecomputeEffectiveDilation();
    }
}

/**
 * @brief 检查指定通道是否处于激活状态
 * @param InChannel 目标通道
 * @return 激活返回 true
 */
bool FGlobalDilationMixer::IsActive(ETimeDilationChannel InChannel) const
{
    const int32 Index = static_cast<int32>(InChannel);
    return ChannelStates.IsValidIndex(Index) ? ChannelStates[Index].bActive : false;
}

/**
 * @brief 获取指定通道当前的时间缩放值
 * @param InChannel 目标通道
 * @return 通道时间缩放值（未激活返回 1.0）
 */
float FGlobalDilationMixer::GetChannelDilation(ETimeDilationChannel InChannel) const
{
    const int32 Index = static_cast<int32>(InChannel);
    return (ChannelStates.IsValidIndex(Index) && ChannelStates[Index].bActive) ? ChannelStates[Index].CurrentDilation : 1.0f;
}

/**
 * @brief 获取当前生效的最终全局时间缩放值
 * @return 生效时间缩放
 */
float FGlobalDilationMixer::GetEffectiveDilation() const
{
    return EffectiveDilation;
}

/**
 * @brief 推进计时与淡出插值，单帧余量在当前通道内连续消费持续与淡出阶段
 * @param InRealDeltaSeconds 真实流逝秒数
 */
void FGlobalDilationMixer::Advance(float InRealDeltaSeconds)
{
    bool bChannelsChanged = false;

    for (FGlobalChannelState& State : ChannelStates) {
        if (!State.bActive) {
            continue;
        }

        float RemainingDelta = InRealDeltaSeconds;
        if (State.RemainingDuration > 0.0f) {
            State.RemainingDuration -= RemainingDelta;
            if (State.RemainingDuration > 0.0f) {
                continue;
            }
            RemainingDelta = -State.RemainingDuration;
            State.RemainingDuration = 0.0f;
            if (State.BlendOutDuration > 0.0f) {
                State.RemainingBlendOut = State.BlendOutDuration;
            } else {
                State.Reset();
                bChannelsChanged = true;
                continue;
            }
        }

        if (State.RemainingBlendOut > 0.0f) {
            State.RemainingBlendOut -= RemainingDelta;
            if (State.RemainingBlendOut <= 0.0f) {
                State.Reset();
                bChannelsChanged = true;
            } else {
                const float Alpha = FMath::Clamp(1.0f - (State.RemainingBlendOut / State.BlendOutDuration), 0.0f, 1.0f);
                State.CurrentDilation = FMath::Lerp(State.TargetDilation, 1.0f, Alpha);
                bChannelsChanged = true;
            }
        }
    }

    if (bChannelsChanged) {
        RecomputeEffectiveDilation();
    }
}

/**
 * @brief 依据通道优先级重算生效时间缩放
 */
void FGlobalDilationMixer::RecomputeEffectiveDilation()
{
    for (const FGlobalChannelState& State : ChannelStates) {
        if (State.bActive) {
            EffectiveDilation = State.CurrentDilation;
            return;
        }
    }
    EffectiveDilation = 1.0f;
}
