/**
 * @file FGameStatAcknowledgedDiff.h
 * @brief 网络复制差分去重器（纯 C++ · 首次到达与变化判定，零组件依赖）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Stat/FGameStatTypes.h"

/**
 * @brief 已确认数值差分记录器
 */
struct GAMEGAMEPLAYCORE_API FGameStatAcknowledgedDiff {
    /**
     * @brief 判定本次有效值是否需要广播，并输出上一次已确认的数值
     * @param InStatTag 属性标签
     * @param InEffectiveCurrent 本次有效当前值
     * @param InEffectiveMax 本次有效上限
     * @param OutOldCurrent 输出上一次已确认的当前值；首次到达时等于本次当前值
     * @param OutOldMax 输出上一次已确认的上限；首次到达时等于本次上限
     * @return 首次到达或数值发生变化返回 true；与已确认值等价返回 false
     */
    [[nodiscard]] bool NeedsBroadcast(
        const FGameplayTag& InStatTag,
        float InEffectiveCurrent,
        float InEffectiveMax,
        float& OutOldCurrent,
        float& OutOldMax
    ) const;

    /**
     * @brief 记录本次已广播的有效数值
     * @param InStatTag 属性标签
     * @param InCurrent 已确认的当前值
     * @param InMax 已确认的上限
     */
    void Record(const FGameplayTag& InStatTag, float InCurrent, float InMax);

    /**
     * @brief 移除指定标签的已确认记录
     * @param InStatTag 属性标签
     */
    void Remove(const FGameplayTag& InStatTag);

    /**
     * @brief 清理已不存在于属性索引中的已确认记录
     * @param InStatIndex 当前属性标签到数组下标的映射
     */
    void PruneMissing(const TMap<FGameplayTag, int32>& InStatIndex);

    /**
     * @brief 清空全部已确认记录
     */
    void Reset();

private:
    TMap<FGameplayTag, FGameStatValue> Acknowledged;
};
