/**
 * @file FDamageChannelBinding.h
 * @brief 伤害通道到 GAS 元属性的输出映射规则（单一职责 · 契约结构体）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"

/**
 * @brief 伤害通道到 GAS 元属性的输出映射规则
 */
struct FDamageChannelBinding {
    /**
     * @brief 匹配的伤害通道标签（例如 Data.Damage.Physical）
     */
    FGameplayTag ChannelTag;

    /**
     * @brief 目标写入的 GAS 元属性（例如 PhysicalDamageAttribute）
     */
    FGameplayAttribute TargetAttribute;

    /**
     * @brief 计算修饰操作（默认 Additive 累加）
     */
    EGameplayModOp::Type ModOp = EGameplayModOp::Additive;

    FDamageChannelBinding() = default;

    /**
     * @brief 构造函数：绑定通道标签、目标输出属性与修饰操作
     * @param InTag 伤害通道标签
     * @param InAttr 目标输出元属性
     * @param InModOp 修饰操作类型（默认加算）
     */
    FDamageChannelBinding(const FGameplayTag& InTag, const FGameplayAttribute& InAttr, EGameplayModOp::Type InModOp = EGameplayModOp::Additive)
        : ChannelTag(InTag), TargetAttribute(InAttr), ModOp(InModOp) {}
};

