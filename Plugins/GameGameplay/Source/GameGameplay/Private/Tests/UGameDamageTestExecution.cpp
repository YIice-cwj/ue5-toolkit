/**
 * @file UGameDamageTestExecution.cpp
 * @brief 自动化测试专用自定义伤害结算子类实现
 */

#include "Tests/UGameDamageTestExecution.h"

#include "Damage/FGameDamageChannelRegistry.h"
#include "Damage/FGameGameplayEffectContext.h"

/**
 * @brief 返回测试配置的伤害修饰器并记录观测到的源/目标聚合标签
 * @param ChannelTag 当前通道
 * @param BaseDamage 基础伤害
 * @param SourceTags 攻击端聚合标签集
 * @param TargetTags 受击端聚合标签集
 * @param ExecutionParams 原始结算上下文
 * @return 测试伤害修饰器
 */
FDamageModifier UGameDamageTestExecution::ComputeChannelModifier(
    const FGameplayTag& ChannelTag,
    float BaseDamage,
    const FGameplayTagContainer& SourceTags,
    const FGameplayTagContainer& TargetTags,
    const FGameplayEffectCustomExecutionParameters& ExecutionParams
) const {
    (void)ChannelTag;
    (void)BaseDamage;
    (void)ExecutionParams;
    LastObservedSourceTags = SourceTags;
    LastObservedTargetTags = TargetTags;
    return TestModifier;
}

/**
 * @brief 结算后注入测试反馈标签/动态数值，并在配置时重入注册表验证零死锁
 * @param ExecutionParams 原始结算上下文
 * @param TotalCalculatedDamage 本轮计算出的总伤害
 * @param OutEffectContext 打击反馈上下文
 */
void UGameDamageTestExecution::OnPostExecuteChannels(
    const FGameplayEffectCustomExecutionParameters& ExecutionParams,
    float TotalCalculatedDamage,
    FGameGameplayEffectContext* OutEffectContext
) const {
    (void)ExecutionParams;
    (void)TotalCalculatedDamage;
    ++PostExecuteCallCount;
    if (OutEffectContext != nullptr) {
        if (PostFeedbackTag.IsValid()) {
            OutEffectContext->AddFeedbackTag(PostFeedbackTag);
        }
        if (PostDynamicValueTag.IsValid()) {
            OutEffectContext->SetDynamicValue(PostDynamicValueTag, PostDynamicValue);
        }
    }
    if (ReentrantRegistrationBinding.IsValid()) {
        (void)FGameDamageChannelRegistry::Get().Register(GetClass(), ReentrantRegistrationBinding);
    }
}
