#include "AbilitySystem/UGameGasBuffBridge.h"

#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "Logging/GameLogCategories.h"

FActiveGameplayEffectHandle UGameGasBuffBridge::ApplyBuffEffect(
    UAbilitySystemComponent* InAbilitySystemComponent,
    TSubclassOf<UGameplayEffect> InBuffEffectClass,
    int32 InLevel,
    UObject* InSourceObject
) {
    if (!IsValid(InAbilitySystemComponent) || InBuffEffectClass == nullptr) {
        GAME_LOG(Warning, TEXT("【Buff 桥接】ASC 或 GameplayEffect 类无效，拒绝施加。"));
        return FActiveGameplayEffectHandle();
    }

    FGameplayEffectContextHandle EffectContext = InAbilitySystemComponent->MakeEffectContext();
    if (InSourceObject != nullptr) {
        EffectContext.AddSourceObject(InSourceObject);
    }

    const FGameplayEffectSpecHandle SpecHandle = InAbilitySystemComponent->MakeOutgoingSpec(InBuffEffectClass, InLevel, EffectContext);
    if (!SpecHandle.IsValid()) {
        GAME_LOG(Warning, TEXT("【Buff 桥接】GameplayEffectSpec 构建失败: %s"), *InBuffEffectClass->GetName());
        return FActiveGameplayEffectHandle();
    }

    return InAbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
}

bool UGameGasBuffBridge::RemoveBuffEffect(UAbilitySystemComponent* InAbilitySystemComponent, FActiveGameplayEffectHandle InHandle, int32 StacksToRemove) {
    if (!IsValid(InAbilitySystemComponent) || !InHandle.IsValid()) {
        return false;
    }

    return InAbilitySystemComponent->RemoveActiveGameplayEffect(InHandle, StacksToRemove);
}

bool UGameGasBuffBridge::IsBuffEffectActive(UAbilitySystemComponent* InAbilitySystemComponent, FActiveGameplayEffectHandle InHandle) {
    if (!IsValid(InAbilitySystemComponent) || !InHandle.IsValid()) {
        return false;
    }

    return InAbilitySystemComponent->GetActiveGameplayEffect(InHandle) != nullptr;
}

int32 UGameGasBuffBridge::GetBuffStackCount(UAbilitySystemComponent* InAbilitySystemComponent, FActiveGameplayEffectHandle InHandle) {
    if (!IsValid(InAbilitySystemComponent) || !InHandle.IsValid()) {
        return 0;
    }

    const FActiveGameplayEffect* ActiveGE = InAbilitySystemComponent->GetActiveGameplayEffect(InHandle);
    return ActiveGE != nullptr ? ActiveGE->Spec.GetStackCount() : 0;
}

float UGameGasBuffBridge::GetBuffRemainingTime(UAbilitySystemComponent* InAbilitySystemComponent, FActiveGameplayEffectHandle InHandle) {
    if (!IsValid(InAbilitySystemComponent) || !InHandle.IsValid()) {
        return 0.0f;
    }

    const FActiveGameplayEffect* ActiveGE = InAbilitySystemComponent->GetActiveGameplayEffect(InHandle);
    if (ActiveGE == nullptr) {
        return 0.0f;
    }

    const UWorld* World = InAbilitySystemComponent->GetWorld();
    return World != nullptr ? ActiveGE->GetTimeRemaining(World->GetTimeSeconds()) : 0.0f;
}

float UGameGasBuffBridge::GetBuffDuration(UAbilitySystemComponent* InAbilitySystemComponent, FActiveGameplayEffectHandle InHandle) {
    if (!IsValid(InAbilitySystemComponent) || !InHandle.IsValid()) {
        return 0.0f;
    }

    const FActiveGameplayEffect* ActiveGE = InAbilitySystemComponent->GetActiveGameplayEffect(InHandle);
    return ActiveGE != nullptr ? ActiveGE->GetDuration() : 0.0f;
}
