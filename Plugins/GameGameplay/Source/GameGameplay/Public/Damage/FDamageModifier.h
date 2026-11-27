/**
 * @file FDamageModifier.h
 * @brief 泛型伤害修饰器（单一职责 · 负责百分比倍率、固定加减与绝对覆写演算）
 */

#pragma once

#include "CoreMinimal.h"

/**
 * @brief 泛型通道伤害修饰器（支持百分比倍率、固定加减减免、绝对锁死覆写）
 */
struct FDamageModifier {
    /**
     * @brief 百分比乘法倍率（默认 1.0f，克制增伤 / 百分比减免）
     */
    float Multiplier = 1.0f;

    /**
     * @brief 加减法固定数值（默认 0.0f，固定破甲 / 固定护甲抵扣）
     */
    float Additive = 0.0f;

    /**
     * @brief 是否启用绝对覆写模式
     */
    bool bOverride = false;

    /**
     * @brief 绝对覆写伤害值（仅 bOverride 为 true 时生效）
     */
    float OverrideValue = 0.0f;

    constexpr FDamageModifier() noexcept = default;

    /**
     * @brief 构造函数：以乘算倍率与固定加减量创建修饰器
     * @param InMult 百分比乘法倍率
     * @param InAdd 加减法固定数值
     */
    constexpr explicit FDamageModifier(float InMult, float InAdd = 0.0f) noexcept
        : Multiplier(InMult)
        , Additive(InAdd) {
    }

    /**
     * @brief 构造绝对覆写修饰器
     * @param InVal 覆写伤害值
     * @return 覆写修饰器实例
     */
    [[nodiscard]] static constexpr FDamageModifier MakeOverride(float InVal) noexcept {
        FDamageModifier Mod;
        Mod.bOverride = true;
        Mod.OverrideValue = InVal;
        return Mod;
    }

    /**
     * @brief 计算修饰后的最终伤害（覆写优先，否则按 (基础 + 加减) * 倍率 演算）
     * @param BaseDamage 输入的基础伤害数值
     * @return 最终修饰后的伤害数值
     */
    [[nodiscard]] FORCEINLINE constexpr float Evaluate(float BaseDamage) const noexcept {
        return bOverride ? OverrideValue : (BaseDamage + Additive) * Multiplier;
    }

    /**
     * @brief 语义值相等比较（覆写模式仅比较覆写值，否则比较倍率与固定加减量）
     * @param Other 待比较对象
     * @return 生效字段均一致返回 true
     */
    [[nodiscard]] bool operator==(const FDamageModifier& Other) const noexcept {
        if (bOverride != Other.bOverride) {
            return false;
        }
        if (bOverride) {
            return FMath::IsNearlyEqual(OverrideValue, Other.OverrideValue);
        }
        return FMath::IsNearlyEqual(Multiplier, Other.Multiplier)
            && FMath::IsNearlyEqual(Additive, Other.Additive);
    }
};
