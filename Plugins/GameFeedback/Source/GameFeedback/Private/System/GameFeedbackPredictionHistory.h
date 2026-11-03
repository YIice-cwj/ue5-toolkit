/**
 * @file GameFeedbackPredictionHistory.h
 * @brief 本地预测表现指纹与网络回执去重（纯逻辑，无 World 依赖）
 */

#pragma once

#include "CoreMinimal.h"

class AActor;

/**
 * @brief 记录本地已播放表现的（攻击者,目标）指纹，用于消费服务器回执
 */
class FGameFeedbackPredictionHistory {
private:
    struct FEntry {
        uint16 PredictionEventId = 0;
        TWeakObjectPtr<AActor> Attacker;
        TWeakObjectPtr<AActor> Target;
        double Timestamp = 0.0;
    };

public:
    /**
     * @brief 默认构造预测历史记录器
     */
    FGameFeedbackPredictionHistory() = default;

    /**
     * @brief 记录一次具有非零跨端事件身份的本地预测表现
     * @param PredictionEventId 跨端预测事件 ID；0 不记录
     * @param Attacker 攻击者实体
     * @param Target 目标实体
     * @param RealTime 当前真实时间（秒）
     */
    void Add(uint16 PredictionEventId, const TWeakObjectPtr<AActor>& Attacker, const TWeakObjectPtr<AActor>& Target, double RealTime);

    /**
     * @brief 尝试消费事件 ID 与双实体均匹配的本地预测记录
     * @param PredictionEventId 跨端预测事件 ID；0 永不匹配
     * @param Attacker 报文中的攻击者实体
     * @param Target 报文中的目标实体
     * @param RealTime 当前真实时间（秒）
     * @return 命中并消费返回 true；无匹配返回 false
     */
    bool ConsumeMatch(uint16 PredictionEventId, const TWeakObjectPtr<AActor>& Attacker, const TWeakObjectPtr<AActor>& Target, double RealTime);

    /**
     * @brief 清理超出有效窗口的指纹
     * @param RealTime 当前真实时间（秒）
     */
    void Prune(double RealTime);

private:
    TArray<FEntry> Entries;
};
