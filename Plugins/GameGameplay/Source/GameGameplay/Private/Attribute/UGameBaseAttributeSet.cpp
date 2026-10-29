/**
 * @file UGameBaseAttributeSet.cpp
 * @brief 属性集通用基类实现（钳制 / 幂等闩 / 元属性搬运 / 增量写入）
 */

#include "Attribute/UGameBaseAttributeSet.h"

#include "AbilitySystemComponent.h"
#include "Logging/GameLogCategories.h"

float UGameBaseAttributeSet::ClampAttribute(float InValue, float InMin, float InMax) noexcept {
    return FMath::Clamp(InValue, InMin, InMax);
}

bool UGameBaseAttributeSet::BroadcastThresholdOnce(UAbilitySystemComponent* InAbilitySystemComponent, const FGameplayTag& InThresholdTag) {
    if (InAbilitySystemComponent == nullptr || InAbilitySystemComponent->HasMatchingGameplayTag(InThresholdTag)) {
        return false;
    }
    InAbilitySystemComponent->AddLooseGameplayTag(InThresholdTag);
    GAME_LOG(Log, TEXT("【属性集】ASC [%s] 触发阈值标签 [%s]。"),
        *GetNameSafe(InAbilitySystemComponent), *InThresholdTag.ToString());
    return true;
}

void UGameBaseAttributeSet::ResetThreshold(UAbilitySystemComponent* InAbilitySystemComponent, const FGameplayTag& InThresholdTag) {
    if (InAbilitySystemComponent != nullptr && InAbilitySystemComponent->HasMatchingGameplayTag(InThresholdTag)) {
        InAbilitySystemComponent->RemoveLooseGameplayTag(InThresholdTag);
        GAME_LOG(Verbose, TEXT("【属性集】ASC [%s] 重置阈值标签 [%s]。"),
            *GetNameSafe(InAbilitySystemComponent), *InThresholdTag.ToString());
    }
}

float UGameBaseAttributeSet::MoveMetaAttribute(FGameplayAttributeData& MetaAttributeData) noexcept {
    const float Value = MetaAttributeData.GetCurrentValue();
    MetaAttributeData.SetBaseValue(0.0f);
    MetaAttributeData.SetCurrentValue(0.0f);
    return Value;
}

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
