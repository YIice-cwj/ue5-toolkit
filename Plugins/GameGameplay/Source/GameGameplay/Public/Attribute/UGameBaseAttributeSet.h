/**
 * @file UGameBaseAttributeSet.h
 * @brief 属性集通用基类（纯机制支撑 · 零具体业务属性假设）
 *
 * 设计思路：框架层只承载"无业务字面"的纯通用机制——阈值标签幂等闩（BroadcastThresholdOnce / ResetThreshold）、
 * 元属性数值提取归零（MoveMetaAttribute）、以及增量落地搬运（ApplyAttributeToLocal）；
 * 彻底剥离任何具体游戏属性与冗余数值钳制包装（数值钳制统一使用引擎原生 FMath::Clamp）；
 * 所有业务属性与元属性 100% 由游戏项目派生子类自由声明与编排。
 */

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "GameplayTagContainer.h"

#include "UGameBaseAttributeSet.generated.h"

class UAbilitySystemComponent;

/**
 * @brief 属性集通用基类（游戏无关，零具体业务属性）
 */
UCLASS(Abstract)
class GAMEGAMEPLAY_API UGameBaseAttributeSet : public UAttributeSet {
    GENERATED_BODY()

public:
    /**
     * @brief 阈值幂等闩：标签有效且未授予则授予松标签并返回 true（本轮首次达阈，调用方据此广播一次）
     * @param InAbilitySystemComponent 目标 ASC
     * @param InThresholdTag 临界状态标签（无效时直接返回 false）
     * @return 是否为本轮首次达阈
     */
    [[nodiscard]] static bool BroadcastThresholdOnce(
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayTag& InThresholdTag
    );

    /**
     * @brief 重置阈值幂等闩：若 ASC 持有该有效阈值标签则移除，使后续可再次触发
     * @param InAbilitySystemComponent 目标 ASC
     * @param InThresholdTag 临界状态标签
     */
    static void ResetThreshold(
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayTag& InThresholdTag
    );

    /**
     * @brief 提取元属性当前数值并立即清零归位（避免元属性在下一结算帧残留 · 强制内联）
     * @param MetaAttributeData 待消费并清零的元属性数据
     * @return 提取出的当前数值
     */
    [[nodiscard]] FORCEINLINE static float MoveMetaAttribute(FGameplayAttributeData& MetaAttributeData) noexcept {
        const float Value = MetaAttributeData.GetCurrentValue();
        MetaAttributeData.SetBaseValue(0.0f);
        MetaAttributeData.SetCurrentValue(0.0f);
        return Value;
    }

    /**
     * @brief 将有限浮点增量搬运落地至目标属性基础值（无效属性或 NaN/Inf 自动拦截）
     * @param InAbilitySystemComponent 目标 ASC
     * @param InTargetAttribute 落地目标属性
     * @param InDelta 有限浮点增量（伤害轨传负值表示扣减）
     */
    static void ApplyAttributeToLocal(
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayAttribute& InTargetAttribute,
        float InDelta
    );
};
