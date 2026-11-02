/**
 * @file GameFeedbackHitStopLimiter.h
 * @brief 顿帧触发冷却与累积时长预算（纯逻辑，无 World 依赖）
 */

#pragma once

#include "CoreMinimal.h"
#include "Feedback/GameFeedbackActions.h"

/**
 * @brief 按 Actor 维护顿帧累积窗口与（攻击者,目标）冷却的纯逻辑限流器
 */
class FGameFeedbackHitStopLimiter {
public:
    FGameFeedbackHitStopLimiter() = default;

    /**
     * @brief 判定当前命中是否已越过（攻击者,目标）冷却窗口
     * @param Attacker 攻击者实体
     * @param Target 目标实体
     * @param Action 顿帧参数
     * @param RealTime 当前真实时间（秒）
     * @return 允许本次触发返回 true；冷却期内返回 false
     */
    bool PassCooldown(AActor* Attacker, AActor* Target, const FGameFeedbackHitStopAction& Action, double RealTime);

    /**
     * @brief 在累积预算内申请一次顿帧时长
     * @param Actor 需要顿帧的实体
     * @param RequestedDuration 期望时长（秒）
     * @param Action 顿帧参数
     * @param RealTime 当前真实时间（秒）
     * @return 实际可应用时长；预算耗尽返回 0
     */
    float AcquireDuration(AActor* Actor, float RequestedDuration, const FGameFeedbackHitStopAction& Action, double RealTime);

    /**
     * @brief 清除指定 Actor 的累积状态与相关冷却记录
     * @param Actor 待清除实体
     */
    void ClearActor(AActor* Actor);

    /**
     * @brief 清理失效弱引用与超期记录
     * @param RealTime 当前真实时间（秒）
     */
    void Prune(double RealTime);

private:
    struct FTrackState {
        float CumulativeDuration = 0.0f;
        double LastTriggerRealTime = 0.0;
    };

    TMap<TWeakObjectPtr<AActor>, FTrackState> Tracks;
    TMap<TPair<TWeakObjectPtr<AActor>, TWeakObjectPtr<AActor>>, double> CooldownMap;
};
