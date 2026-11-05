/**
 * @file UGameBaseAttributeSet.cpp
 * @brief 属性集通用基类实现（阈值幂等闩与增量写入）
 */

#include "Attribute/UGameBaseAttributeSet.h"

#include "AbilitySystemComponent.h"
#include "Logging/GameLogCategories.h"

/**
 * @brief 阈值幂等闩：标签未授予则授予并返回 true
 * @param InAbilitySystemComponent 目标 ASC
 * @param InThresholdTag 临界状态标签
 * @return 是否为本轮首次达阈
 */
bool UGameBaseAttributeSet::BroadcastThresholdOnce(UAbilitySystemComponent* InAbilitySystemComponent, const FGameplayTag& InThresholdTag) {
    if (InAbilitySystemComponent == nullptr || InAbilitySystemComponent->HasMatchingGameplayTag(InThresholdTag)) {
        return false;
    }
    InAbilitySystemComponent->AddLooseGameplayTag(InThresholdTag);
    GAME_LOG(Log, TEXT("【属性集】ASC [%s] 触发阈值标签 [%s]。"),
        *GetNameSafe(InAbilitySystemComponent), *InThresholdTag.ToString());
    return true;
}

/**
 * @brief 重置阈值幂等闩：若 ASC 持有该阈值标签则移除
 * @param InAbilitySystemComponent 目标 ASC
 * @param InThresholdTag 临界状态标签
 */
void UGameBaseAttributeSet::ResetThreshold(UAbilitySystemComponent* InAbilitySystemComponent, const FGameplayTag& InThresholdTag) {
    if (InAbilitySystemComponent != nullptr && InAbilitySystemComponent->HasMatchingGameplayTag(InThresholdTag)) {
        InAbilitySystemComponent->RemoveLooseGameplayTag(InThresholdTag);
        GAME_LOG(Verbose, TEXT("【属性集】ASC [%s] 重置阈值标签 [%s]。"),
            *GetNameSafe(InAbilitySystemComponent), *InThresholdTag.ToString());
    }
}

/**
 * @brief 将增量搬运落地至目标属性
 * @param InAbilitySystemComponent 目标 ASC
 * @param InTargetAttribute 落地目标属性
 * @param InDelta 增量（伤害轨传负值表示扣减）
 */
void UGameBaseAttributeSet::ApplyAttributeToLocal(UAbilitySystemComponent* InAbilitySystemComponent, const FGameplayAttribute& InTargetAttribute, float InDelta) {
    if (InAbilitySystemComponent == nullptr) {
        return;
    }
    const float OldBase = InAbilitySystemComponent->GetNumericAttributeBase(InTargetAttribute);
    const float NewBase = OldBase + InDelta;
    InAbilitySystemComponent->SetNumericAttributeBase(InTargetAttribute, NewBase);
    GAME_LOG(Log, TEXT("【属性集】ASC [%s] 本地增量修改属性 [%s]: 增量=%.2f, 基础值: %.2f -> %.2f。"),
        *GetNameSafe(InAbilitySystemComponent), *InTargetAttribute.GetName(), InDelta, OldBase, NewBase);
}
