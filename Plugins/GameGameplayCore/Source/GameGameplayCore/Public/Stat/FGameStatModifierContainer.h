/**
 * @file FGameStatModifierContainer.h
 * @brief 属性修正器管理容器（单一职责 · 负责加乘覆写求值运算与句柄生命周期）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Stat/FGameStatTypes.h"

/**
 * @brief 属性修饰器容器（无 UObject 开销的高性能修饰器管理与演算器）
 */
struct GAMEGAMEPLAYCORE_API FGameStatModifierContainer {
public:
    FGameStatModifierContainer() = default;

    /**
     * @brief 注册一条属性修正器
     * @param InStatTag 目标属性标签
     * @param InModifier 修正规则
     * @return 修正器句柄，标签非法时返回 0
     */
    uint32 AddModifier(const FGameplayTag& InStatTag, const FGameStatModifier& InModifier);

    /**
     * @brief 按句柄注销一条属性修正器
     * @param InStatTag 目标属性标签
     * @param InHandle 修正器句柄
     * @return 是否成功注销
     */
    bool RemoveModifier(const FGameplayTag& InStatTag, uint32 InHandle);

    /**
     * @brief 清空指定属性的全部修正器
     * @param InStatTag 目标属性标签
     * @return 被清除的修正器数量
     */
    int32 ClearModifiers(const FGameplayTag& InStatTag);

    /**
     * @brief 判定指定属性是否有适用的修正器
     * @param InStatTag 目标属性标签
     * @param bForMax 是否查询上限适用
     * @return 存在适用修正器返回 true
     */
    [[nodiscard]] bool HasApplicableModifiers(const FGameplayTag& InStatTag, bool bForMax = false) const;

    /**
     * @brief 评估叠加修正器后的数值（加算 -> 乘算 -> 覆写）
     * @param InStatTag 目标属性标签
     * @param BaseValue 基础数值
     * @param bForMax 是否评估上限
     * @return 修饰后的数值
     */
    [[nodiscard]] float EvaluateModifiers(const FGameplayTag& InStatTag, float BaseValue, bool bForMax = false) const;

    /**
     * @brief 重置全部修正器数据
     */
    void Reset();

private:
    TMap<FGameplayTag, TArray<FGameStatModifierEntry>> ModifiersByTag;
    uint32 NextHandle = 1;
};
