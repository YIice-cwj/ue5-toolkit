/**
 * @file FGameStatEffectiveEvaluator.cpp
 * @brief 属性有效值演算器实现
 */

#include "Stat/FGameStatEffectiveEvaluator.h"

#include "Stat/FGameStatModifierContainer.h"

float FGameStatEffectiveEvaluator::EvaluateCurrent(
    float InBaseCurrent,
    float InMin,
    const FGameplayTag& InStatTag,
    const FGameStatModifierContainer& InModifiers
) {
    if (!InModifiers.HasApplicableModifiers(InStatTag, false)) {
        return InBaseCurrent;
    }
    return FMath::Max(InModifiers.EvaluateModifiers(InStatTag, InBaseCurrent, false), InMin);
}

float FGameStatEffectiveEvaluator::EvaluateMax(
    float InBaseMax,
    float InMin,
    const FGameplayTag& InStatTag,
    const FGameStatModifierContainer& InModifiers
) {
    if (!InModifiers.HasApplicableModifiers(InStatTag, true)) {
        return InBaseMax;
    }
    return FMath::Max(InModifiers.EvaluateModifiers(InStatTag, InBaseMax, true), InMin);
}

float FGameStatEffectiveEvaluator::EvaluatePercent(float InCurrent, float InMax, float InMin) {
    const float Range = InMax - InMin;
    return (Range > 0.0f) ? FMath::Clamp((InCurrent - InMin) / Range, 0.0f, 1.0f) : 0.0f;
}
