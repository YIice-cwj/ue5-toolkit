/**
 * @file FActorHitStopTable.cpp
 * @brief Actor 局部顿帧表实现
 */

#include "Time/FActorHitStopTable.h"

#include "GameFramework/Actor.h"

namespace {
constexpr float MinTimeDilationScale = 0.0001f;
constexpr float MaxTimeDilationScale = 20.0f;
}

/**
 * @brief 请求对单个 Actor 施加顿帧（同一 Actor 重复请求取较大时长与较小缩放；InDuration <= 0 时忽略）
 * @param InTarget 目标 Actor
 * @param InDuration 顿帧持续真实时间（秒，须 > 0）
 * @param InTimeDilation 顿帧期间局部时间缩放（钳制到 [0.0001, 20]）
 */
void FActorHitStopTable::Request(AActor& InTarget, float InDuration, float InTimeDilation) {
    if (!(InDuration > 0.0f)) {
        return;
    }

    const float ClampedDilation = FMath::Clamp(InTimeDilation, MinTimeDilationScale, MaxTimeDilationScale);
    const TObjectKey<AActor> ActorKey(&InTarget);
    if (FActorHitStopState* ExistingState = ActiveActorHitStops.Find(ActorKey)) {
        ExistingState->RemainingDuration = FMath::Max(ExistingState->RemainingDuration, InDuration);
        ExistingState->HitStopTimeDilation = FMath::Min(ExistingState->HitStopTimeDilation, ClampedDilation);
        InTarget.CustomTimeDilation = ExistingState->HitStopTimeDilation;
        return;
    }

    FActorHitStopState NewState;
    NewState.OriginalCustomTimeDilation = InTarget.CustomTimeDilation;
    NewState.HitStopTimeDilation = ClampedDilation;
    NewState.RemainingDuration = InDuration;

    InTarget.CustomTimeDilation = ClampedDilation;
    ActiveActorHitStops.Add(ActorKey, NewState);
}

/**
 * @brief 清除指定 Actor 的顿帧并立即还原时间缩放
 * @param InTarget 目标 Actor
 */
void FActorHitStopTable::Clear(AActor* InTarget) {
    if (InTarget == nullptr) {
        return;
    }

    const TObjectKey<AActor> ActorKey(InTarget);
    FActorHitStopState* ExistingState = ActiveActorHitStops.Find(ActorKey);
    if (ExistingState == nullptr) {
        return;
    }

    InTarget->CustomTimeDilation = ExistingState->OriginalCustomTimeDilation;
    ActiveActorHitStops.Remove(ActorKey);
}

/**
 * @brief 清除全部顿帧并立即还原各自时间缩放
 */
void FActorHitStopTable::ClearAll() {
    for (const TPair<TObjectKey<AActor>, FActorHitStopState>& Pair : ActiveActorHitStops) {
        if (AActor* Actor = Pair.Key.ResolveObjectPtr(); IsValid(Actor)) {
            Actor->CustomTimeDilation = Pair.Value.OriginalCustomTimeDilation;
        }
    }
    ActiveActorHitStops.Empty();
}

/**
 * @brief 检查指定 Actor 是否处于顿帧中
 * @param InTarget 目标 Actor
 * @return 处于顿帧中返回 true
 */
bool FActorHitStopTable::IsActive(const AActor* InTarget) const {
    return InTarget != nullptr && ActiveActorHitStops.Contains(TObjectKey<AActor>(InTarget));
}

/**
 * @brief 推进顿帧计时，到期或目标失效时还原时间缩放并移除记录（InRealDeltaSeconds <= 0 时忽略）
 * @param InRealDeltaSeconds 真实流逝秒数
 */
void FActorHitStopTable::Advance(float InRealDeltaSeconds) {
    if (!(InRealDeltaSeconds > 0.0f)) {
        return;
    }

    for (auto Iterator = ActiveActorHitStops.CreateIterator(); Iterator; ++Iterator) {
        AActor* Actor = Iterator.Key().ResolveObjectPtr();
        if (!IsValid(Actor)) {
            Iterator.RemoveCurrent();
            continue;
        }

        FActorHitStopState& State = Iterator.Value();
        State.RemainingDuration -= InRealDeltaSeconds;
        if (State.RemainingDuration <= 0.0f) {
            Actor->CustomTimeDilation = State.OriginalCustomTimeDilation;
            Iterator.RemoveCurrent();
        }
    }
}

/**
 * @brief 清理目标已失效或正在销毁的记录（不归还时间缩放）
 */
void FActorHitStopTable::PruneInvalid() {
    for (auto Iterator = ActiveActorHitStops.CreateIterator(); Iterator; ++Iterator) {
        if (!IsValid(Iterator.Key().ResolveObjectPtr())) {
            Iterator.RemoveCurrent();
        }
    }
}
