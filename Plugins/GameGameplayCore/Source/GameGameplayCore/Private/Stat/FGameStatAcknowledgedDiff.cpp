/**
 * @file FGameStatAcknowledgedDiff.cpp
 * @brief 网络复制差分去重器实现
 */

#include "Stat/FGameStatAcknowledgedDiff.h"

bool FGameStatAcknowledgedDiff::NeedsBroadcast(
    const FGameplayTag& InStatTag,
    float InEffectiveCurrent,
    float InEffectiveMax,
    float& OutOldCurrent,
    float& OutOldMax
) const {
    const FGameStatValue* Previous = Acknowledged.Find(InStatTag);
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

void FGameStatAcknowledgedDiff::Record(const FGameplayTag& InStatTag, float InCurrent, float InMax) {
    FGameStatValue& Entry = Acknowledged.FindOrAdd(InStatTag);
    Entry.Current = InCurrent;
    Entry.Max = InMax;
}

void FGameStatAcknowledgedDiff::Remove(const FGameplayTag& InStatTag) {
    Acknowledged.Remove(InStatTag);
}

void FGameStatAcknowledgedDiff::PruneMissing(const TMap<FGameplayTag, int32>& InStatIndex) {
    for (auto Iterator = Acknowledged.CreateIterator(); Iterator; ++Iterator) {
        if (!InStatIndex.Contains(Iterator.Key())) {
            Iterator.RemoveCurrent();
        }
    }
}

void FGameStatAcknowledgedDiff::Reset() {
    Acknowledged.Reset();
}
