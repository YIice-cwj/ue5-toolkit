/**
 * @file UGameAbilitySystemComponent.cpp
 * @brief 通用能力系统组件基类实现
 */

#include "AbilitySystem/UGameAbilitySystemComponent.h"

#include "AbilitySystemGlobals.h"

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
}
