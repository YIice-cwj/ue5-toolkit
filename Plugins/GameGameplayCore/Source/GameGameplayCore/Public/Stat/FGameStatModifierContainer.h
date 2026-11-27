/**
 * @file FGameStatModifierContainer.h
 * @brief 属性修正器管理容器（单一职责 · 负责加乘覆写求值运算、原地原子更新、下限钳制与代际句柄生命周期）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Stat/FGameStatTypes.h"

/**
 * @brief 属性修饰器容器（内联小数组零堆分配 + 代际句柄防 ABA 误删 + 单趟双轴演算）
 */
struct GAMEGAMEPLAYCORE_API FGameStatModifierContainer {
public:
    FGameStatModifierContainer() = default;

    /**
     * @brief 注册一条属性修正器
     * @param InStatTag 目标属性标签
     * @param InModifier 修正规则（Value 须为有限浮点）
     * @return 强类型代际修正器句柄；标签非法或数值非有限时返回无效句柄
     */
    [[nodiscard]] FGameStatModifierHandle AddModifier(const FGameplayTag& InStatTag, const FGameStatModifier& InModifier);

    /**
     * @brief 按句柄原地更新一条已注册修正器（保持插入顺序与句柄不变，零堆分配）
     * @param InStatTag 目标属性标签
     * @param InHandle 目标修正器代际句柄
     * @param InNewModifier 新的修正规则（Value 须为有限浮点）
     * @return 句柄命中且成功更新返回 true，否则返回 false
     */
    bool UpdateModifier(
        const FGameplayTag& InStatTag,
        FGameStatModifierHandle InHandle,
        const FGameStatModifier& InNewModifier
    );

    /**
     * @brief 按句柄注销一条属性修正器（保持其余修正器插入顺序以维持覆写优先级）
     * @param InStatTag 目标属性标签
     * @param InHandle 修正器代际句柄
     * @return 成功注销返回 true，否则返回 false
     */
    bool RemoveModifier(const FGameplayTag& InStatTag, FGameStatModifierHandle InHandle);

    /**
     * @brief 清空指定属性的全部修正器
     * @param InStatTag 目标属性标签
     * @return 被清除的修正器数量
     */
    int32 ClearModifiers(const FGameplayTag& InStatTag);

    /**
     * @brief 评估叠加修正器后的原始数值（加算 -> 乘算 -> 后置覆写优先，不执行下限钳制）
     * @param InStatTag 目标属性标签
     * @param BaseValue 基础数值
     * @param bForMax 是否评估上限
     * @return 修饰后的数值
     */
    [[nodiscard]] float EvaluateModifiers(const FGameplayTag& InStatTag, float BaseValue, bool bForMax = false) const;

    /**
     * @brief 评估叠加修正器并与下限钳制后的有效数值（无论是否存在修正器均与 MinValue 取较大值）
     * @param InStatTag 目标属性标签
     * @param BaseValue 基础数值
     * @param MinValue 属性基础下限
     * @param bForMax 是否评估上限
     * @return 演算结果与下限 MinValue 中的较大者
     */
    [[nodiscard]] float EvaluateClamped(
        const FGameplayTag& InStatTag,
        float BaseValue,
        float MinValue,
        bool bForMax = false
    ) const;

    /**
     * @brief 单趟同时演算叠加修正器并下限钳制后的有效当前值与有效上限
     * @param InStatTag 目标属性标签
     * @param InBaseValue 基础属性数值结构
     * @return 包含有效当前值与有效上限的成对结果
     */
    [[nodiscard]] FGameStatEffectivePair EvaluateEffectivePair(
        const FGameplayTag& InStatTag,
        const FGameStatValue& InBaseValue
    ) const;

    /**
     * @brief 重置全部修正器数据并递增代际号，使此前签发的所有历史句柄立即失效
     */
    void Reset();

private:
    using FModifierArray = TArray<FGameStatModifierEntry, TInlineAllocator<4>>;

    TMap<FGameplayTag, FModifierArray> ModifiersByTag;
    uint32 NextSequence = 1u;
    uint32 Generation = 1u;
};
