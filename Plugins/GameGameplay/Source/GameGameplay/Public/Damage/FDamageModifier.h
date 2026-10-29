/**
 * @file FDamageModifier.h
 * @brief 泛型伤害修饰器与通道绑定契约（单一职责 · 负责通道到元属性映射与倍率/加减/覆写演算）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "Damage/FDamageChannelBinding.h"

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

    FDamageModifier() = default;

    /**
     * @brief 构造函数：以乘算倍率与固定加减量创建修饰器
     * @param InMult 百分比乘法倍率
     * @param InAdd 加减法固定数值
     */
    explicit FDamageModifier(float InMult, float InAdd = 0.0f) : Multiplier(InMult), Additive(InAdd) {}

    /**
     * @brief 构造绝对覆写修饰器
     * @param InVal 覆写伤害值
     * @return 覆写修饰器实例
     */
    static FDamageModifier MakeOverride(float InVal) {
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
    [[nodiscard]] FORCEINLINE float Evaluate(float BaseDamage) const noexcept {
        return bOverride ? OverrideValue : (BaseDamage + Additive) * Multiplier;
    }
};
