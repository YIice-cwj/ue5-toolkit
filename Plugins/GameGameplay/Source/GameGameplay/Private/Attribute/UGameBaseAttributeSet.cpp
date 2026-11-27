/**
 * @file UGameBaseAttributeSet.cpp
 * @brief 属性集通用基类实现（阈值幂等闩与增量写入）
 */

#include "Attribute/UGameBaseAttributeSet.h"

#include "AbilitySystemComponent.h"
#include "GameGameplayLog.h"

/**
 * @brief 阈值幂等闩：标签有效且未授予则授予松标签并返回 true
 * @param InAbilitySystemComponent 目标 ASC
 * @param InThresholdTag 临界状态标签
 * @return 是否为本轮首次达阈
 */
bool UGameBaseAttributeSet::BroadcastThresholdOnce(
    UAbilitySystemComponent* InAbilitySystemComponent,
    const FGameplayTag& InThresholdTag
) {
    if (InAbilitySystemComponent == nullptr || !InThresholdTag.IsValid() || InAbilitySystemComponent->HasMatchingGameplayTag(InThresholdTag)) {
        return false;
    }
    InAbilitySystemComponent->AddLooseGameplayTag(InThresholdTag);
    GAME_GAS_LOG(
        LogGameAttribute,
        Verbose,
        TEXT("【属性集】ASC [%s] 触发阈值标签 [%s]。"),
        *GetNameSafe(InAbilitySystemComponent),
        *InThresholdTag.ToString()
    );
    return true;
}

/**
 * @brief 重置阈值幂等闩：若 ASC 持有该有效阈值标签则移除
 * @param InAbilitySystemComponent 目标 ASC
 * @param InThresholdTag 临界状态标签
 */
void UGameBaseAttributeSet::ResetThreshold(
    UAbilitySystemComponent* InAbilitySystemComponent,
    const FGameplayTag& InThresholdTag
) {
    if (InAbilitySystemComponent == nullptr || !InThresholdTag.IsValid()) {
        return;
    }
    if (InAbilitySystemComponent->HasMatchingGameplayTag(InThresholdTag)) {
        InAbilitySystemComponent->RemoveLooseGameplayTag(InThresholdTag);
        GAME_GAS_LOG(
            LogGameAttribute,
            Verbose,
            TEXT("【属性集】ASC [%s] 重置阈值标签 [%s]。"),
            *GetNameSafe(InAbilitySystemComponent),
            *InThresholdTag.ToString()
        );
    }
}

/**
 * @brief 将有限浮点增量搬运落地至目标属性基础值
 * @param InAbilitySystemComponent 目标 ASC
 * @param InTargetAttribute 落地目标属性
 * @param InDelta 有限浮点增量（伤害轨传负值表示扣减）
 */
void UGameBaseAttributeSet::ApplyAttributeToLocal(
    UAbilitySystemComponent* InAbilitySystemComponent,
    const FGameplayAttribute& InTargetAttribute,
    float InDelta
) {
    if (InAbilitySystemComponent == nullptr || !InTargetAttribute.IsValid()) {
        return;
    }
    if (!FMath::IsFinite(InDelta)) {
        GAME_GAS_LOG(
            LogGameAttribute,
            Warning,
            TEXT("【属性集】ASC [%s] 拒绝非有限增量修改属性 [%s]。"),
            *GetNameSafe(InAbilitySystemComponent),
            *InTargetAttribute.GetName()
        );
        return;
    }

    const float OldBase = InAbilitySystemComponent->GetNumericAttributeBase(InTargetAttribute);
    const float NewBase = OldBase + InDelta;
    InAbilitySystemComponent->SetNumericAttributeBase(InTargetAttribute, NewBase);
    GAME_GAS_LOG(
        LogGameAttribute,
        Verbose,
        TEXT("【属性集】ASC [%s] 本地增量修改属性 [%s]: 增量=%.2f, 基础值: %.2f -> %.2f。"),
        *GetNameSafe(InAbilitySystemComponent),
        *InTargetAttribute.GetName(),
        InDelta,
        OldBase,
        NewBase
    );
}
