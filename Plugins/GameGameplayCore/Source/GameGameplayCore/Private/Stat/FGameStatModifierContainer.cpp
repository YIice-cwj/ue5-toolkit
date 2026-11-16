/**
 * @file FGameStatModifierContainer.cpp
 * @brief 属性修正器管理容器实现
 */

#include "Stat/FGameStatModifierContainer.h"

/**
 * @brief 注册一条属性修正器
 * @param InStatTag 目标属性标签
 * @param InModifier 修正规则
 * @return 修正器句柄，标签非法时返回 0
 */
uint32 FGameStatModifierContainer::AddModifier(const FGameplayTag& InStatTag, const FGameStatModifier& InModifier) {
    if (!InStatTag.IsValid()) {
        return 0;
    }

    const uint32 AllocatedHandle = NextHandle++;
    TArray<FGameStatModifierEntry>& Entries = ModifiersByTag.FindOrAdd(InStatTag);
    Entries.Add(FGameStatModifierEntry{AllocatedHandle, InModifier});
    return AllocatedHandle;
}

/**
 * @brief 按句柄注销一条属性修正器
 * @param InStatTag 目标属性标签
 * @param InHandle 修正器句柄
 * @return 成功注销返回 true，否则返回 false
 */
bool FGameStatModifierContainer::RemoveModifier(const FGameplayTag& InStatTag, uint32 InHandle) {
    if (!InStatTag.IsValid() || InHandle == 0) {
        return false;
    }

    TArray<FGameStatModifierEntry>* Entries = ModifiersByTag.Find(InStatTag);
    if (Entries == nullptr) {
        return false;
    }

    const int32 RemovedCount = Entries->RemoveAll([InHandle](const FGameStatModifierEntry& Entry) {
        return Entry.Handle == InHandle;
    });

    if (Entries->IsEmpty()) {
        ModifiersByTag.Remove(InStatTag);
    }

    return RemovedCount > 0;
}

/**
 * @brief 清空指定属性的全部修正器
 * @param InStatTag 目标属性标签
 * @return 被清除的修正器数量
 */
int32 FGameStatModifierContainer::ClearModifiers(const FGameplayTag& InStatTag) {
    if (!InStatTag.IsValid()) {
        return 0;
    }

    TArray<FGameStatModifierEntry> RemovedEntries;
    if (ModifiersByTag.RemoveAndCopyValue(InStatTag, RemovedEntries)) {
        return RemovedEntries.Num();
    }

    return 0;
}

/**
 * @brief 判定指定属性是否有适用的修正器
 * @param InStatTag 目标属性标签
 * @param bForMax 是否查询上限适用
 * @return 存在适用修正器返回 true，否则返回 false
 */
bool FGameStatModifierContainer::HasApplicableModifiers(const FGameplayTag& InStatTag, bool bForMax) const {
    const TArray<FGameStatModifierEntry>* Entries = ModifiersByTag.Find(InStatTag);
    if (Entries == nullptr || Entries->IsEmpty()) {
        return false;
    }

    return Entries->ContainsByPredicate([bForMax](const FGameStatModifierEntry& Entry) {
        return bForMax ? Entry.Modifier.bApplyToMax : Entry.Modifier.bApplyToCurrent;
    });
}

/**
 * @brief 评估叠加修正器后的数值（加算 -> 乘算 -> 覆写）
 * @param InStatTag 目标属性标签
 * @param BaseValue 基础数值
 * @param bForMax 是否评估上限
 * @return 叠加修正器后的数值
 */
float FGameStatModifierContainer::EvaluateModifiers(const FGameplayTag& InStatTag, float BaseValue, bool bForMax) const {
    const TArray<FGameStatModifierEntry>* Entries = ModifiersByTag.Find(InStatTag);
    if (Entries == nullptr || Entries->IsEmpty()) {
        return BaseValue;
    }

    float AdditiveSum = 0.0f;
    float MultiplierSum = 1.0f;
    float OverrideValue = 0.0f;
    bool bHasOverride = false;
    bool bAppliedAny = false;

    for (const FGameStatModifierEntry& Entry : *Entries) {
        const FGameStatModifier& Mod = Entry.Modifier;
        const bool bApplicable = bForMax ? Mod.bApplyToMax : Mod.bApplyToCurrent;
        if (!bApplicable) {
            continue;
        }

        bAppliedAny = true;
        switch (Mod.Op) {
            case EGameStatModifierOp::Add:
                AdditiveSum += Mod.Value;
                break;
            case EGameStatModifierOp::Multiply:
                MultiplierSum *= Mod.Value;
                break;
            case EGameStatModifierOp::Override:
                OverrideValue = Mod.Value;
                bHasOverride = true;
                break;
        }
    }

    if (!bAppliedAny) {
        return BaseValue;
    }

    if (bHasOverride) {
        return OverrideValue;
    }
    return (BaseValue + AdditiveSum) * MultiplierSum;
}

/**
 * @brief 重置全部修正器数据
 */
void FGameStatModifierContainer::Reset() {
    ModifiersByTag.Reset();
    NextHandle = 1;
}
