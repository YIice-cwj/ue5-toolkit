/**
 * @file FGameStatEffectiveEvaluator.h
 * @brief 属性有效值演算器（纯 C++ · 修正器叠加与下限钳制统一入口，零组件依赖）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

struct FGameStatModifierContainer;

/**
 * @brief 属性有效值演算器
 */
struct GAMEGAMEPLAYCORE_API FGameStatEffectiveEvaluator {
    /**
     * @brief 演算叠加修正器后的有效当前值
     * @param InBaseCurrent 基础当前值
     * @param InMin 属性基础下限
     * @param InStatTag 属性标签
     * @param InModifiers 修正器容器
     * @return 无适用修正器时返回基础当前值，否则返回演算结果与下限的较大者
     */
    [[nodiscard]] static float EvaluateCurrent(
        float InBaseCurrent,
        float InMin,
        const FGameplayTag& InStatTag,
        const FGameStatModifierContainer& InModifiers
    );

    /**
     * @brief 演算叠加修正器后的有效上限
     * @param InBaseMax 基础上限
     * @param InMin 属性基础下限
     * @param InStatTag 属性标签
     * @param InModifiers 修正器容器
     * @return 无适用修正器时返回基础上限，否则返回演算结果与下限的较大者
     */
    [[nodiscard]] static float EvaluateMax(
        float InBaseMax,
        float InMin,
        const FGameplayTag& InStatTag,
        const FGameStatModifierContainer& InModifiers
    );

    /**
     * @brief 按当前值与上限计算归一化百分比
     * @param InCurrent 当前值
     * @param InMax 上限
     * @param InMin 下限
     * @return 钳制到 [0.0, 1.0] 的百分比，区间非法时返回 0.0
     */
    [[nodiscard]] static float EvaluatePercent(float InCurrent, float InMax, float InMin);
};
