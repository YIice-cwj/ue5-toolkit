/**
 * @file UGameAbilitySystemComponent.cpp
 * @brief 通用能力系统组件基类实现
 */

#include "Ability/UGameAbilitySystemComponent.h"

#include "AbilitySystemGlobals.h"
#include "Logging/GameLogCategories.h"

/**
 * @brief 构造函数：开启默认复制，并把 GE 与能力容器注册到自身拥有者
 */
UGameAbilitySystemComponent::UGameAbilitySystemComponent() {
    SetIsReplicatedByDefault(true);
    ActiveGameplayEffects.RegisterWithOwner(this);
    ActivatableAbilities.RegisterWithOwner(this);
}

/**
 * @brief 初始化能力宿主信息（确保 AbilityActorInfo 已分配，交由基类建立宿主关联合同并记录日志）
 * @param InOwnerActor 拥有者 Actor
 * @param InAvatarActor 附身 Actor
 */
void UGameAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) {
    if (!AbilityActorInfo.IsValid()) {
        AbilityActorInfo = TSharedPtr<FGameplayAbilityActorInfo>(UAbilitySystemGlobals::Get().AllocAbilityActorInfo());
    }
    Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);
    GAME_LOG(Log, TEXT("【GAS 组件】初始化能力 Actor 信息: Owner=[%s], Avatar=[%s]。"),
        *GetNameSafe(InOwnerActor), *GetNameSafe(InAvatarActor));
}
