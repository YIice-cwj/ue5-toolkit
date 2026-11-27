/**
 * @file UGameGameplayAbility.cpp
 * @brief GAS 能力基类实现
 */

#include "Ability/UGameGameplayAbility.h"

#include "AbilitySystemComponent.h"
#include "GameGameplayLog.h"

/**
 * @brief 构造函数：实例化策略设为 InstancedPerActor、复制策略设为 ReplicateNo
 */
UGameGameplayAbility::UGameGameplayAbility() {
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateNo;
}

/**
 * @brief 通用激活否决守卫：引擎判定通过后快速阻断死亡 / 硬直等状态标签并零分配收集命中标签
 * @param Handle 能力规格句柄
 * @param ActorInfo 能力宿主信息
 * @param SourceTags 来源标签
 * @param TargetTags 目标标签
 * @param OptionalRelevantTags 可选相关标签输出
 * @return 允许激活返回 true
 */
bool UGameGameplayAbility::CanActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayTagContainer* SourceTags,
    const FGameplayTagContainer* TargetTags,
    OUT FGameplayTagContainer* OptionalRelevantTags
) const {
    if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)) {
        return false;
    }

    if (BlockingStateTags.IsEmpty() || ActorInfo == nullptr) {
        return true;
    }

    const UAbilitySystemComponent* AbilitySystemComponent = ActorInfo->AbilitySystemComponent.Get();
    if (AbilitySystemComponent == nullptr || !AbilitySystemComponent->HasAnyMatchingGameplayTags(BlockingStateTags)) {
        return true;
    }

    if (OptionalRelevantTags != nullptr) {
        for (const FGameplayTag& BlockingTag : BlockingStateTags) {
            if (AbilitySystemComponent->HasMatchingGameplayTag(BlockingTag)) {
                OptionalRelevantTags->AddTag(BlockingTag);
            }
        }
    }
    GAME_GAS_LOG(LogGameAbility, Verbose, TEXT("【GAS 能力】能力 [%s] 激活被阻断：宿主命中阻断状态标签。"), *GetName());
    return false;
}

/**
 * @brief 统一激活入口：记录触发来源后转交 GAS 原生激活路径并记录详细日志
 * @param Handle 能力规格句柄
 * @param ActorInfo 能力宿主信息
 * @param ActivationInfo 激活信息（承载本地预测 / 服务器授权模式）
 * @param TriggerEventData 触发事件数据
 */
void UGameGameplayAbility::ActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData
) {
    TriggerSource = ResolveTriggerSource(TriggerEventData);
    GAME_GAS_LOG(
        LogGameAbility,
        Verbose,
        TEXT("【GAS 能力】能力 [%s] 激活: 触发来源=%d, Avatar=[%s]。"),
        *GetName(),
        static_cast<int32>(TriggerSource),
        ActorInfo != nullptr ? *GetNameSafe(ActorInfo->AvatarActor.Get()) : TEXT("None")
    );
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

/**
 * @brief 能力结束回调：先转交基类完成结束委托与蓝图通知，确认不再活跃后再复位触发来源
 * @param Handle 能力规格句柄
 * @param ActorInfo 能力宿主信息
 * @param ActivationInfo 激活信息
 * @param bReplicateEndAbility 是否复制结束通知
 * @param bWasCancelled 是否因取消而结束
 */
void UGameGameplayAbility::EndAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility,
    bool bWasCancelled
) {
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
    if (!IsActive()) {
        TriggerSource = EGameAbilityTriggerSource::Input;
    }
}

/**
 * @brief 由触发事件数据解析触发来源
 * @param InTriggerEventData 触发事件数据（为空即视为输入触发）
 * @return 触发来源
 */
EGameAbilityTriggerSource UGameGameplayAbility::ResolveTriggerSource(const FGameplayEventData* InTriggerEventData) const {
    if (InTriggerEventData == nullptr) {
        return EGameAbilityTriggerSource::Input;
    }
    if (const EGameAbilityTriggerSource* Found = TriggerSourceTagMap.Find(InTriggerEventData->EventTag)) {
        return *Found;
    }
    return EGameAbilityTriggerSource::Effect;
}
