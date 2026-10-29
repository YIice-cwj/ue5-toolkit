/**
 * @file UGameAbilitySystemComponent.cpp
 * @brief 通用能力系统组件基类实现
 */

#include "Ability/UGameAbilitySystemComponent.h"

#include "AbilitySystemGlobals.h"
#include "Logging/GameLogCategories.h"

UGameAbilitySystemComponent::UGameAbilitySystemComponent() {
    SetIsReplicatedByDefault(true);
    ActiveGameplayEffects.RegisterWithOwner(this);
    ActivatableAbilities.RegisterWithOwner(this);
}

void UGameAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) {
    if (!AbilityActorInfo.IsValid()) {
        AbilityActorInfo = TSharedPtr<FGameplayAbilityActorInfo>(UAbilitySystemGlobals::Get().AllocAbilityActorInfo());
    }
    Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);
    GAME_LOG(Log, TEXT("【GAS 组件】初始化能力 Actor 信息: Owner=[%s], Avatar=[%s]。"),
        *GetNameSafe(InOwnerActor), *GetNameSafe(InAvatarActor));
}
