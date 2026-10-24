/**
 * @file UGameBaseAttributeSet.h
 * @brief 属性集通用基类（纯机制支撑 · 零具体业务属性假设）
 *
 * 设计思路：框架层只承载"无业务字面"的纯通用机制——属性上下限钳制、阈值标签幂等闩、
 * 元属性数值提取归零（MoveMetaAttribute）、以及增量落地搬运（ApplyAttributeToLocal）；
 * 彻底剥离任何具体游戏属性（如生命值、护盾、物理伤害、法术伤害等）；
 * 所有业务属性与元属性 100% 由游戏项目派生子类（如 UHeroAttributeSet）自由声明与编排。
 */

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"

#include "GameGameplay.h"
#include "UGameBaseAttributeSet.generated.h"

/**
 * @brief 属性集通用基类（游戏无关，零具体业务属性）
 */
UCLASS(Abstract)
class GAMEGAMEPLAY_API UGameBaseAttributeSet : public UAttributeSet {
    GENERATED_BODY()

public:
    /**
     * @brief 属性上下限钳制（框架层唯一钳制入口）
     * @param InValue 待钳制数值
     * @param InMin 下限
     * @param InMax 上限
     * @return 钳制后的数值
     */
    [[nodiscard]] static float ClampAttribute(float InValue, float InMin, float InMax) noexcept;

    /**
     * @brief 阈值幂等闩：标签未授予则授予并返回 true（本轮首次达阈，调用方据此广播一次）
     * @param InAbilitySystemComponent 目标 ASC
     * @param InThresholdTag 临界状态标签
     * @return 是否为本轮首次达阈
     *
     * 实现思路：ASC 缺失或已持有该标签时返回 false，未持有则授予松标签并返回 true；
     * 松标签不参与复制，故本闩为服务端幂等语义，客户端表现随状态复制在表现层落地
     */
    [[nodiscard]] static bool BroadcastThresholdOnce(UAbilitySystemComponent* InAbilitySystemComponent, const FGameplayTag& InThresholdTag);

    /**
     * @brief 重置阈值幂等闩：若 ASC 持有该阈值标签则移除，使后续可再次触发
     * @param InAbilitySystemComponent 目标 ASC
     * @param InThresholdTag 临界状态标签
     */
    static void ResetThreshold(UAbilitySystemComponent* InAbilitySystemComponent, const FGameplayTag& InThresholdTag);

    /**
     * @brief 提取元属性当前数值并立即清零归位（避免元属性在下一结算帧残留）
     * @param MetaAttributeData 待消费并清零的元属性数据
     * @return 提取出的当前数值
     */
    static float MoveMetaAttribute(FGameplayAttributeData& MetaAttributeData) noexcept;

    /**
     * @brief 将增量搬运落地至目标属性（框架层唯一属性增量写入入口）
     * @param InAbilitySystemComponent 目标 ASC
     * @param InTargetAttribute 落地目标属性
     * @param InDelta 增量（伤害轨传负值表示扣减）
     */
    static void ApplyAttributeToLocal(UAbilitySystemComponent* InAbilitySystemComponent, const FGameplayAttribute& InTargetAttribute, float InDelta);
};
