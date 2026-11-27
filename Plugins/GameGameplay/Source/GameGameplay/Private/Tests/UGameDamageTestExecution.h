/**
 * @file UGameDamageTestExecution.h
 * @brief 自动化测试专用自定义伤害结算子类（验证修饰钩子、反馈注入、Spec 标签聚合与重入注册防死锁）
 */

#pragma once

#include "CoreMinimal.h"
#include "Damage/FDamageChannelBinding.h"
#include "Damage/UGameDamageExecution.h"

#include "UGameDamageTestExecution.generated.h"

/**
 * @brief 仅用于自动化测试的 UGameDamageExecution 具体派生类
 */
UCLASS(NotBlueprintable)
class UGameDamageTestExecution : public UGameDamageExecution {
    GENERATED_BODY()

public:
    /**
     * @brief 配置用于测试的修饰器覆盖规则
     */
    FDamageModifier TestModifier;

    /**
     * @brief 配置在 OnPostExecuteChannels 中注入的反馈标签
     */
    FGameplayTag PostFeedbackTag;

    /**
     * @brief 配置在 OnPostExecuteChannels 中注入的动态数值标签与大小
     */
    FGameplayTag PostDynamicValueTag;
    float PostDynamicValue = 0.0f;

    /**
     * @brief 若有效，则在 OnPostExecuteChannels 中动态注册此通道以验证锁外防重入死锁
     */
    FDamageChannelBinding ReentrantRegistrationBinding;

    /**
     * @brief 记录最近一次 ComputeChannelModifier 观测到的聚合标签与回调次数
     */
    mutable FGameplayTagContainer LastObservedSourceTags;
    mutable FGameplayTagContainer LastObservedTargetTags;
    mutable int32 PostExecuteCallCount = 0;

protected:
    /**
     * @brief 返回测试配置的伤害修饰器并记录观测到的源/目标聚合标签
     * @param ChannelTag 当前通道
     * @param BaseDamage 基础伤害
     * @param SourceTags 攻击端聚合标签集
     * @param TargetTags 受击端聚合标签集
     * @param ExecutionParams 原始结算上下文
     * @return 测试伤害修饰器
     */
    FDamageModifier ComputeChannelModifier(
        const FGameplayTag& ChannelTag,
        float BaseDamage,
        const FGameplayTagContainer& SourceTags,
        const FGameplayTagContainer& TargetTags,
        const FGameplayEffectCustomExecutionParameters& ExecutionParams
    ) const override;

    /**
     * @brief 结算后注入测试反馈标签/动态数值，并在配置时重入注册表验证零死锁
     * @param ExecutionParams 原始结算上下文
     * @param TotalCalculatedDamage 本轮计算出的总伤害
     * @param OutEffectContext 打击反馈上下文
     */
    void OnPostExecuteChannels(
        const FGameplayEffectCustomExecutionParameters& ExecutionParams,
        float TotalCalculatedDamage,
        FGameGameplayEffectContext* OutEffectContext
    ) const override;
};
