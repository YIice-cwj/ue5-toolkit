/**
 * @file FGlobalDilationMixer.cpp
 * @brief 全局分层时间膨胀混音器实现
 */

#include "Time/FGlobalDilationMixer.h"

FGlobalDilationMixer::FGlobalDilationMixer() {
    ChannelStates.SetNum(static_cast<int32>(ETimeDilationChannel::Count));
}

void FGlobalDilationMixer::Request(ETimeDilationChannel InChannel, float InDuration, float InTimeDilation, float InBlendOutDuration) {
    const int32 Index = static_cast<int32>(InChannel);
    if (!ChannelStates.IsValidIndex(Index)) {
        return;
    }

    FGlobalChannelState& State = ChannelStates[Index];
    State.TargetDilation = FMath::Clamp(InTimeDilation, 0.0f, 20.0f);
    State.CurrentDilation = State.TargetDilation;
    State.TotalDuration = FMath::Max(0.0f, InDuration);
    State.RemainingDuration = State.TotalDuration;
    State.BlendOutDuration = FMath::Max(0.0f, InBlendOutDuration);
    State.RemainingBlendOut = 0.0f;
    State.bActive = true;

    RecomputeEffectiveDilation();
}

void FGlobalDilationMixer::Clear(ETimeDilationChannel InChannel) {
    const int32 Index = static_cast<int32>(InChannel);
    if (!ChannelStates.IsValidIndex(Index) || !ChannelStates[Index].bActive) {
        return;
    }

    ChannelStates[Index].Reset();
    RecomputeEffectiveDilation();
}

void FGlobalDilationMixer::ClearWithBlend(ETimeDilationChannel InChannel, float InBlendOutDuration) {
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

void FGlobalDilationMixer::ClearAll() {
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

bool FGlobalDilationMixer::IsActive(ETimeDilationChannel InChannel) const {
    const int32 Index = static_cast<int32>(InChannel);
    return ChannelStates.IsValidIndex(Index) ? ChannelStates[Index].bActive : false;
}

float FGlobalDilationMixer::GetChannelDilation(ETimeDilationChannel InChannel) const {
    const int32 Index = static_cast<int32>(InChannel);
    return (ChannelStates.IsValidIndex(Index) && ChannelStates[Index].bActive) ? ChannelStates[Index].CurrentDilation : 1.0f;
}

float FGlobalDilationMixer::GetEffectiveDilation() const {
    return EffectiveDilation;
}

void FGlobalDilationMixer::Advance(float InRealDeltaSeconds) {
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

void FGlobalDilationMixer::RecomputeEffectiveDilation() {
    for (const FGlobalChannelState& State : ChannelStates) {
        if (State.bActive) {
            EffectiveDilation = State.CurrentDilation;
            return;
        }
    }
    EffectiveDilation = 1.0f;
}
