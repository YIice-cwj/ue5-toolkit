// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tracker/GameFeedbackHitStopLimiter.h"

#include "Feedback/GameFeedbackActions.h"
#include "GameFramework/Actor.h"

namespace {
constexpr double StaleStateLifetimeSeconds = 5.0;
constexpr double PruneIntervalSeconds = 1.0;
constexpr int32 PruneEntryThreshold = 64;
}

/**
 * @brief 判定当前命中是否已越过（攻击者,目标）冷却窗口
 * @param Attacker 攻击者实体
 * @param Target 目标实体
 * @param Action 顿帧参数
 * @param RealTime 当前真实时间（秒）
 * @return 允许本次触发返回 true；冷却期内返回 false
 */
bool FGameFeedbackHitStopLimiter::PassCooldown(AActor* Attacker, AActor* Target, const FGameFeedbackHitStopAction& Action, double RealTime) {
    if (!Attacker && !Target) {
        return true;
    }

    const TPair<TObjectKey<AActor>, TObjectKey<AActor>> PairKey{TObjectKey<AActor>(Attacker), TObjectKey<AActor>(Target)};
    if (double* LastTriggerTime = CooldownMap.Find(PairKey)) {
        if (RealTime - *LastTriggerTime < Action.CooldownInterval) {
            return false;
        }
    }

    CooldownMap.Add(PairKey, RealTime);
    return true;
}

/**
 * @brief 在累积预算内申请一次顿帧时长
 * @param Actor 需要顿帧的实体
 * @param RequestedDuration 期望时长（秒）
 * @param Action 顿帧参数
 * @param RealTime 当前真实时间（秒）
 * @return 实际可应用时长；预算耗尽返回 0
 */
float FGameFeedbackHitStopLimiter::AcquireDuration(AActor* Actor, float RequestedDuration, const FGameFeedbackHitStopAction& Action, double RealTime) {
    if (!Actor || RequestedDuration <= 0.0f) {
        return 0.0f;
    }

    FTrackState& Track = Tracks.FindOrAdd(TObjectKey<AActor>(Actor));
    Track.ResetWindow = FMath::Max(Track.ResetWindow, Action.ResetWindow);
    if (RealTime - Track.LastTriggerRealTime > Action.ResetWindow) {
        Track.CumulativeDuration = 0.0f;
    }

    if (Track.CumulativeDuration >= Action.MaxCumulativeDuration) {
        return 0.0f;
    }

    const float AppliedDuration = FMath::Min(RequestedDuration, Action.MaxCumulativeDuration - Track.CumulativeDuration);
    Track.CumulativeDuration += AppliedDuration;
    Track.LastTriggerRealTime = RealTime;
    return AppliedDuration;
}

/**
 * @brief 清除指定 Actor 的累积状态与相关冷却记录
 * @param Actor 待清除实体
 */
void FGameFeedbackHitStopLimiter::ClearActor(AActor* Actor) {
    if (!Actor) {
        return;
    }

    const TObjectKey<AActor> ActorKey(Actor);
    Tracks.Remove(ActorKey);
    for (auto It = CooldownMap.CreateIterator(); It; ++It) {
        if (It.Key().Key == ActorKey || It.Key().Value == ActorKey) {
            It.RemoveCurrent();
        }
    }
}

/**
 * @brief 清理失效弱引用与超期记录
 * @param RealTime 当前真实时间（秒）
 */
void FGameFeedbackHitStopLimiter::Prune(double RealTime) {
    LastPruneRealTime = RealTime;

    for (auto It = Tracks.CreateIterator(); It; ++It) {
        const double EffectiveLifetime = FMath::Max(StaleStateLifetimeSeconds, static_cast<double>(It.Value().ResetWindow));
        if (!IsValid(It.Key().ResolveObjectPtr()) || RealTime - It.Value().LastTriggerRealTime > EffectiveLifetime) {
            It.RemoveCurrent();
        }
    }

    const TObjectKey<AActor> NullActorKey;
    for (auto It = CooldownMap.CreateIterator(); It; ++It) {
        const TObjectKey<AActor>& AttackerKey = It.Key().Key;
        const TObjectKey<AActor>& TargetKey = It.Key().Value;
        const bool bAttackerDead = (AttackerKey != NullActorKey) && !IsValid(AttackerKey.ResolveObjectPtr());
        const bool bTargetDead = (TargetKey != NullActorKey) && !IsValid(TargetKey.ResolveObjectPtr());
        const bool bExpired = (RealTime - It.Value() > StaleStateLifetimeSeconds);
        if (bAttackerDead || bTargetDead || bExpired) {
            It.RemoveCurrent();
        }
    }
}

/**
 * @brief 按时间间隔或容量阈值节流执行清理，避免每次命中全表扫描
 * @param RealTime 当前真实时间（秒）
 */
void FGameFeedbackHitStopLimiter::PruneIfNeeded(double RealTime) {
    const int32 TotalEntries = Tracks.Num() + CooldownMap.Num();
    if (TotalEntries == 0) {
        return;
    }
    if (TotalEntries >= PruneEntryThreshold || (RealTime - LastPruneRealTime) >= PruneIntervalSeconds) {
        Prune(RealTime);
    }
}

/**
 * @brief 重置全部累积状态与冷却记录
 */
void FGameFeedbackHitStopLimiter::Reset() {
    Tracks.Empty();
    CooldownMap.Empty();
    LastPruneRealTime = -1.0e9;
}
