// Copyright Epic Games, Inc. All Rights Reserved.

#include "System/GameFeedbackPredictionHistory.h"

namespace {
constexpr double PredictionMatchWindowSeconds = 0.5;
}

/**
 * @brief 记录一次具有非零跨端事件身份的本地预测表现
 * @param PredictionEventId 跨端预测事件 ID；0 不记录
 * @param Attacker 攻击者实体
 * @param Target 目标实体
 * @param RealTime 当前真实时间（秒）
 */
void FGameFeedbackPredictionHistory::Add(uint16 PredictionEventId, const TWeakObjectPtr<AActor>& Attacker, const TWeakObjectPtr<AActor>& Target, double RealTime)
{
    if (PredictionEventId == 0) {
        return;
    }

    Prune(RealTime);

    FEntry& Entry = Entries.Emplace_GetRef();
    Entry.PredictionEventId = PredictionEventId;
    Entry.Attacker = Attacker;
    Entry.Target = Target;
    Entry.Timestamp = RealTime;
}

/**
 * @brief 尝试消费事件 ID 与双实体均匹配的本地预测记录
 * @param PredictionEventId 跨端预测事件 ID；0 永不匹配
 * @param Attacker 报文中的攻击者实体
 * @param Target 报文中的目标实体
 * @param RealTime 当前真实时间（秒）
 * @return 命中并消费返回 true；无匹配返回 false
 */
bool FGameFeedbackPredictionHistory::ConsumeMatch(uint16 PredictionEventId, const TWeakObjectPtr<AActor>& Attacker, const TWeakObjectPtr<AActor>& Target, double RealTime)
{
    if (PredictionEventId == 0) {
        return false;
    }

    Prune(RealTime);

    for (int32 Index = 0; Index < Entries.Num(); ++Index) {
        if (Entries[Index].PredictionEventId == PredictionEventId
            && Entries[Index].Attacker == Attacker.Get()
            && Entries[Index].Target == Target.Get()) {
            Entries.RemoveAtSwap(Index);
            return true;
        }
    }

    return false;
}

/**
 * @brief 清理超出有效窗口的指纹
 * @param RealTime 当前真实时间（秒）
 */
void FGameFeedbackPredictionHistory::Prune(double RealTime)
{
    Entries.RemoveAll([RealTime](const FEntry& Entry) {
        return RealTime - Entry.Timestamp > PredictionMatchWindowSeconds;
    });
}
