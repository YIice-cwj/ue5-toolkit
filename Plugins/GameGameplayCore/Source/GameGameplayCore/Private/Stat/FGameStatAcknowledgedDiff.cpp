/**
 * @file FGameStatAcknowledgedDiff.cpp
 * @brief 网络复制差分去重器实现
 */

#include "Stat/FGameStatAcknowledgedDiff.h"

/**
 * @brief 判定本次有效值是否需要广播，并输出上一次已确认的数值
 * @param InStatTag 属性标签
 * @param InEffectiveCurrent 本次有效当前值
 * @param InEffectiveMax 本次有效上限
 * @param OutOldCurrent 输出上一次已确认的当前值；首次到达时等于本次当前值
 * @param OutOldMax 输出上一次已确认的上限；首次到达时等于本次上限
 * @return 首次到达或数值发生变化返回 true；与已确认值等价返回 false
 */
bool FGameStatAcknowledgedDiff::NeedsBroadcast(
    const FGameplayTag& InStatTag,
    float InEffectiveCurrent,
    float InEffectiveMax,
    float& OutOldCurrent,
    float& OutOldMax
) const {
    const FGameStatEffectivePair* Previous = Acknowledged.Find(FGameFastTagKey(InStatTag));
    if (Previous == nullptr) {
        OutOldCurrent = InEffectiveCurrent;
        OutOldMax = InEffectiveMax;
        return true;
    }

    OutOldCurrent = Previous->Current;
    OutOldMax = Previous->Max;
    return !FMath::IsNearlyEqual(Previous->Current, InEffectiveCurrent)
        || !FMath::IsNearlyEqual(Previous->Max, InEffectiveMax);
}

/**
 * @brief 记录本次已广播的有效数值
 * @param InStatTag 属性标签
 * @param InCurrent 已确认的当前值
 * @param InMax 已确认的上限
 */
void FGameStatAcknowledgedDiff::Record(const FGameplayTag& InStatTag, float InCurrent, float InMax) {
    FGameStatEffectivePair& Entry = Acknowledged.FindOrAdd(FGameFastTagKey(InStatTag));
    Entry.Current = InCurrent;
    Entry.Max = InMax;
}

/**
 * @brief 移除指定标签的已确认记录
 * @param InStatTag 属性标签
 */
void FGameStatAcknowledgedDiff::Remove(const FGameplayTag& InStatTag) {
    Acknowledged.Remove(FGameFastTagKey(InStatTag));
}

/**
 * @brief 清理未满足保留谓词的已确认记录
 * @param IsKeyPresent 判定属性标签是否仍存在的谓词闭包
 */
void FGameStatAcknowledgedDiff::PruneMissing(TFunctionRef<bool(const FGameplayTag&)> IsKeyPresent) {
    for (int32 Index = Acknowledged.Num() - 1; Index >= 0; --Index) {
        const FGameFastTagKey Key = Acknowledged.GetEntries()[Index].Key;
        if (!IsKeyPresent(Key.Tag)) {
            Acknowledged.Remove(Key);
        }
    }
}

/**
 * @brief 清空全部已确认记录
 */
void FGameStatAcknowledgedDiff::Reset() {
    Acknowledged.Reset();
}
