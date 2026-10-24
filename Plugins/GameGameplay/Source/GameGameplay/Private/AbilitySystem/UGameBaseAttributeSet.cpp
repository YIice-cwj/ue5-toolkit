/**
 * @file UGameBaseAttributeSet.cpp
 * @brief 属性集通用基类实现（钳制 / 幂等闩 / 元属性搬运 / 增量写入）
 */

#include "AbilitySystem/UGameBaseAttributeSet.h"

#include "AbilitySystemComponent.h"

float UGameBaseAttributeSet::ClampAttribute(float InValue, float InMin, float InMax) noexcept {
    return FMath::Clamp(InValue, InMin, InMax);
}

bool UGameBaseAttributeSet::BroadcastThresholdOnce(UAbilitySystemComponent* InAbilitySystemComponent, const FGameplayTag& InThresholdTag) {
    if (InAbilitySystemComponent == nullptr || InAbilitySystemComponent->HasMatchingGameplayTag(InThresholdTag)) {
        return false;
    }
    InAbilitySystemComponent->AddLooseGameplayTag(InThresholdTag);
    return true;
}

void UGameBaseAttributeSet::ResetThreshold(UAbilitySystemComponent* InAbilitySystemComponent, const FGameplayTag& InThresholdTag) {
    if (InAbilitySystemComponent != nullptr && InAbilitySystemComponent->HasMatchingGameplayTag(InThresholdTag)) {
        InAbilitySystemComponent->RemoveLooseGameplayTag(InThresholdTag);
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
    InAbilitySystemComponent->SetNumericAttributeBase(InTargetAttribute, InAbilitySystemComponent->GetNumericAttributeBase(InTargetAttribute) + InDelta);
}
