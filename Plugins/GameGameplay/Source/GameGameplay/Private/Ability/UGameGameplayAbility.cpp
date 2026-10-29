/**
 * @file UGameGameplayAbility.cpp
 * @brief GAS 能力基类实现
 */

#include "Ability/UGameGameplayAbility.h"

#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"
#include "Logging/GameLogCategories.h"

UGameGameplayAbility::UGameGameplayAbility() {
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateYes;
}

/**
 * @brief 通用激活否决守卫：引擎判定通过后追加死亡 / 硬直等状态标签短路
 * @param Handle 能力规格句柄
 * @param ActorInfo 能力宿主信息
 * @param SourceTags 来源标签
 * @param TargetTags 目标标签
 * @param OptionalRelevantTags 可选相关标签输出
 * @return 允许激活返回 true
 */
bool UGameGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, OUT FGameplayTagContainer* OptionalRelevantTags) const {
    if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)) {
        return false;
    }
    const UAbilitySystemComponent* AbilitySystemComponent = ActorInfo->AbilitySystemComponent.Get();
    if (AbilitySystemComponent == nullptr) {
        return false;
    }
    if (AbilitySystemComponent->HasAnyMatchingGameplayTags(BlockingStateTags)) {
        if (OptionalRelevantTags != nullptr) {
            OptionalRelevantTags->AppendTags(AbilitySystemComponent->GetOwnedGameplayTags().Filter(BlockingStateTags));
        }
        GAME_LOG(Verbose, TEXT("【GAS 能力】能力 [%s] 激活被阻断：宿主命中阻断状态标签。"), *GetName());
        return false;
    }
    return true;
}

/**
 * @brief 统一激活入口：记录触发来源后转交 GAS 原生激活路径并记录日志
 * @param Handle 能力规格句柄
 * @param ActorInfo 能力宿主信息
 * @param ActivationInfo 激活信息（承载本地预测 / 服务器授权模式）
 * @param TriggerEventData 触发事件数据
 */
void UGameGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) {
    TriggerSource = ResolveTriggerSource(TriggerEventData);
    GAME_LOG(Log, TEXT("【GAS 能力】能力 [%s] 激活: 触发来源=%d, Avatar=[%s]。"),
        *GetName(), static_cast<int32>(TriggerSource),
        ActorInfo != nullptr ? *GetNameSafe(ActorInfo->AvatarActor.Get()) : TEXT("None"));
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

/**
 * @brief 注册网络复制属性：触发来源随能力实例复制到远端，供远端表现层分叉
 * @param OutLifetimeProps 输出属性列表
 */
void UGameGameplayAbility::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UGameGameplayAbility, TriggerSource);
}

/**
 * @brief 最近一次激活的触发来源
 * @return 触发来源
 */
EGameAbilityTriggerSource UGameGameplayAbility::GetTriggerSource() const noexcept {
    return TriggerSource;
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
