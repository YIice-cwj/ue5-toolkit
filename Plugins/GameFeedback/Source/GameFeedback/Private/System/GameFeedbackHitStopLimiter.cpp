// Copyright Epic Games, Inc. All Rights Reserved.

#include "System/GameFeedbackHitStopLimiter.h"

namespace {
constexpr double StaleStateLifetimeSeconds = 5.0;
}

/**
 * @brief 判定当前命中是否已越过（攻击者,目标）冷却窗口
 * @param Attacker 攻击者实体
 * @param Target 目标实体
 * @param Action 顿帧参数
 * @param RealTime 当前真实时间（秒）
 * @return 允许本次触发返回 true；冷却期内返回 false
 */
bool FGameFeedbackHitStopLimiter::PassCooldown(AActor* Attacker, AActor* Target, const FGameFeedbackHitStopAction& Action, double RealTime)
{
    const auto PairKey = MakeTuple(TWeakObjectPtr<AActor>(Attacker), TWeakObjectPtr<AActor>(Target));
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
float FGameFeedbackHitStopLimiter::AcquireDuration(AActor* Actor, float RequestedDuration, const FGameFeedbackHitStopAction& Action, double RealTime)
{
    if (!Actor || RequestedDuration <= 0.0f) {
        return 0.0f;
    }

    FTrackState& Track = Tracks.FindOrAdd(Actor);
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
void FGameFeedbackHitStopLimiter::ClearActor(AActor* Actor)
{
    if (!Actor) {
        return;
    }

    Tracks.Remove(Actor);
    for (auto It = CooldownMap.CreateIterator(); It; ++It) {
        if (It.Key().Key == Actor || It.Key().Value == Actor) {
            It.RemoveCurrent();
        }
    }
}

/**
 * @brief 清理失效弱引用与超期记录
 * @param RealTime 当前真实时间（秒）
 */
void FGameFeedbackHitStopLimiter::Prune(double RealTime)
{
    for (auto It = Tracks.CreateIterator(); It; ++It) {
        if (!It.Key().IsValid() || RealTime - It.Value().LastTriggerRealTime > StaleStateLifetimeSeconds) {
            It.RemoveCurrent();
        }
    }

    for (auto It = CooldownMap.CreateIterator(); It; ++It) {
        if (!It.Key().Key.IsValid() || !It.Key().Value.IsValid() || RealTime - It.Value() > StaleStateLifetimeSeconds) {
            It.RemoveCurrent();
        }
    }
}
