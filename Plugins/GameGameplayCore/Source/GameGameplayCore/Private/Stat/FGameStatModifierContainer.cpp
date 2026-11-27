/**
 * @file FGameStatModifierContainer.cpp
 * @brief 属性修正器管理容器实现
 */

#include "Stat/FGameStatModifierContainer.h"

/**
 * @brief 注册一条属性修正器
 * @param InStatTag 目标属性标签
 * @param InModifier 修正规则（Value 须为有限浮点）
 * @return 强类型代际修正器句柄；标签非法或数值非有限时返回无效句柄
 */
FGameStatModifierHandle FGameStatModifierContainer::AddModifier(
    const FGameplayTag& InStatTag,
    const FGameStatModifier& InModifier
) {
    if (!InStatTag.IsValid() || !FMath::IsFinite(InModifier.Value)) {
        return FGameStatModifierHandle();
    }

    if (NextSequence == FGameStatModifierHandle::INVALID_INDEX) {
        NextSequence = 1u;
        Generation = (Generation < MAX_uint32) ? (Generation + 1u) : 1u;
    }

    const FGameStatModifierHandle AllocatedHandle(NextSequence++, Generation);
    FModifierArray& Entries = ModifiersByTag.FindOrAdd(InStatTag);
    Entries.Add(FGameStatModifierEntry{AllocatedHandle, InModifier});
    return AllocatedHandle;
}

/**
 * @brief 按句柄原地更新一条已注册修正器（保持插入顺序与句柄不变，零堆分配）
 * @param InStatTag 目标属性标签
 * @param InHandle 目标修正器代际句柄
 * @param InNewModifier 新的修正规则（Value 须为有限浮点）
 * @return 句柄命中且成功更新返回 true，否则返回 false
 */
bool FGameStatModifierContainer::UpdateModifier(
    const FGameplayTag& InStatTag,
    FGameStatModifierHandle InHandle,
    const FGameStatModifier& InNewModifier
) {
    if (!InStatTag.IsValid() || !InHandle.IsValid() || !FMath::IsFinite(InNewModifier.Value)) {
        return false;
    }

    FModifierArray* Entries = ModifiersByTag.Find(InStatTag);
    if (Entries == nullptr) {
        return false;
    }

    for (FGameStatModifierEntry& Entry : *Entries) {
        if (Entry.Handle == InHandle) {
            Entry.Modifier = InNewModifier;
            return true;
        }
    }
    return false;
}

/**
 * @brief 按句柄注销一条属性修正器（保持其余修正器插入顺序以维持覆写优先级）
 * @param InStatTag 目标属性标签
 * @param InHandle 修正器代际句柄
 * @return 成功注销返回 true，否则返回 false
 */
bool FGameStatModifierContainer::RemoveModifier(const FGameplayTag& InStatTag, FGameStatModifierHandle InHandle) {
    if (!InStatTag.IsValid() || !InHandle.IsValid()) {
        return false;
    }

    FModifierArray* Entries = ModifiersByTag.Find(InStatTag);
    if (Entries == nullptr) {
        return false;
    }

    const int32 MatchIndex = Entries->IndexOfByPredicate([InHandle](const FGameStatModifierEntry& Entry) {
        return Entry.Handle == InHandle;
    });
    if (MatchIndex == INDEX_NONE) {
        return false;
    }

    Entries->RemoveAt(MatchIndex);
    if (Entries->IsEmpty()) {
        ModifiersByTag.Remove(InStatTag);
    }
    return true;
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

    const FModifierArray* Entries = ModifiersByTag.Find(InStatTag);
    if (Entries == nullptr) {
        return 0;
    }

    const int32 RemovedCount = Entries->Num();
    ModifiersByTag.Remove(InStatTag);
    return RemovedCount;
}

/**
 * @brief 评估叠加修正器后的原始数值（加算 -> 乘算 -> 后置覆写优先，不执行下限钳制）
 * @param InStatTag 目标属性标签
 * @param BaseValue 基础数值
 * @param bForMax 是否评估上限
 * @return 叠加修正器后的数值
 */
float FGameStatModifierContainer::EvaluateModifiers(const FGameplayTag& InStatTag, float BaseValue, bool bForMax) const {
    const FModifierArray* Entries = ModifiersByTag.Find(InStatTag);
    if (Entries == nullptr || Entries->IsEmpty()) {
        return BaseValue;
    }

    float AdditiveSum = 0.0f;
    float MultiplierProduct = 1.0f;
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
                MultiplierProduct *= Mod.Value;
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
    return (BaseValue + AdditiveSum) * MultiplierProduct;
}

/**
 * @brief 评估叠加修正器并与下限钳制后的有效数值（无论是否存在修正器均与 MinValue 取较大值）
 * @param InStatTag 目标属性标签
 * @param BaseValue 基础数值
 * @param MinValue 属性基础下限
 * @param bForMax 是否评估上限
 * @return 演算结果与下限 MinValue 中的较大者
 */
float FGameStatModifierContainer::EvaluateClamped(
    const FGameplayTag& InStatTag,
    float BaseValue,
    float MinValue,
    bool bForMax
) const {
    return FMath::Max(EvaluateModifiers(InStatTag, BaseValue, bForMax), MinValue);
}

/**
 * @brief 单趟同时演算叠加修正器并下限钳制后的有效当前值与有效上限
 * @param InStatTag 目标属性标签
 * @param InBaseValue 基础属性数值结构
 * @return 包含有效当前值与有效上限的成对结果
 */
FGameStatEffectivePair FGameStatModifierContainer::EvaluateEffectivePair(
    const FGameplayTag& InStatTag,
    const FGameStatValue& InBaseValue
) const {
    const FModifierArray* Entries = ModifiersByTag.Find(InStatTag);
    if (Entries == nullptr || Entries->IsEmpty()) {
        return FGameStatEffectivePair{
            FMath::Max(InBaseValue.Current, InBaseValue.Min),
            FMath::Max(InBaseValue.Max, InBaseValue.Min)
        };
    }

    float CurrentAdd = 0.0f;
    float CurrentMult = 1.0f;
    float CurrentOverride = 0.0f;
    bool bHasCurrentOverride = false;
    bool bAppliedCurrent = false;

    float MaxAdd = 0.0f;
    float MaxMult = 1.0f;
    float MaxOverride = 0.0f;
    bool bHasMaxOverride = false;
    bool bAppliedMax = false;

    for (const FGameStatModifierEntry& Entry : *Entries) {
        const FGameStatModifier& Mod = Entry.Modifier;
        if (Mod.bApplyToCurrent) {
            bAppliedCurrent = true;
            switch (Mod.Op) {
                case EGameStatModifierOp::Add:
                    CurrentAdd += Mod.Value;
                    break;
                case EGameStatModifierOp::Multiply:
                    CurrentMult *= Mod.Value;
                    break;
                case EGameStatModifierOp::Override:
                    CurrentOverride = Mod.Value;
                    bHasCurrentOverride = true;
                    break;
            }
        }
        if (Mod.bApplyToMax) {
            bAppliedMax = true;
            switch (Mod.Op) {
                case EGameStatModifierOp::Add:
                    MaxAdd += Mod.Value;
                    break;
                case EGameStatModifierOp::Multiply:
                    MaxMult *= Mod.Value;
                    break;
                case EGameStatModifierOp::Override:
                    MaxOverride = Mod.Value;
                    bHasMaxOverride = true;
                    break;
            }
        }
    }

    const float EvaluatedCurrent = !bAppliedCurrent
        ? InBaseValue.Current
        : (bHasCurrentOverride ? CurrentOverride : (InBaseValue.Current + CurrentAdd) * CurrentMult);
    const float EvaluatedMax = !bAppliedMax
        ? InBaseValue.Max
        : (bHasMaxOverride ? MaxOverride : (InBaseValue.Max + MaxAdd) * MaxMult);

    return FGameStatEffectivePair{
        FMath::Max(EvaluatedCurrent, InBaseValue.Min),
        FMath::Max(EvaluatedMax, InBaseValue.Min)
    };
}

/**
 * @brief 重置全部修正器数据并递增代际号，使此前签发的所有历史句柄立即失效
 */
void FGameStatModifierContainer::Reset() {
    ModifiersByTag.Reset();
    NextSequence = 1u;
    Generation = (Generation < MAX_uint32) ? (Generation + 1u) : 1u;
}
