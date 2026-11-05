/**
 * @file UGameGasBuffBridge.cpp
 * @brief GAS Buff 桥接实现（以 GameplayEffect 为载体的统一增删查入口）
 */

#include "Bridge/UGameGasBuffBridge.h"

#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "Logging/GameLogCategories.h"

/**
 * @brief 以 GameplayEffect 为载体施加 Buff
 * @param InAbilitySystemComponent 目标 ASC
 * @param InBuffEffectClass Buff 使用的 GameplayEffect 类
 * @param InLevel 效果等级
 * @param InSourceObject 施加来源对象
 * @return 激活效果句柄，失败返回无效句柄
 */
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

    const FActiveGameplayEffectHandle AppliedHandle = InAbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
    if (AppliedHandle.IsValid()) {
        GAME_LOG(Log, TEXT("【Buff 桥接】成功桥接施加 GameplayEffect: 类=[%s], 等级=%d, 目标=[%s]。"),
            *InBuffEffectClass->GetName(), InLevel, *GetNameSafe(InAbilitySystemComponent->GetOwner()));
    }
    return AppliedHandle;
}

/**
 * @brief 移除指定激活的 Buff 效果
 * @param InAbilitySystemComponent 目标 ASC
 * @param InHandle 激活效果句柄
 * @param StacksToRemove 移除层数（-1 表示移除全部层数）
 * @return 是否移除成功
 */
bool UGameGasBuffBridge::RemoveBuffEffect(UAbilitySystemComponent* InAbilitySystemComponent, FActiveGameplayEffectHandle InHandle, int32 StacksToRemove) {
    if (!IsValid(InAbilitySystemComponent) || !InHandle.IsValid()) {
        return false;
    }

    const bool bSuccess = InAbilitySystemComponent->RemoveActiveGameplayEffect(InHandle, StacksToRemove);
    if (bSuccess) {
        GAME_LOG(Log, TEXT("【Buff 桥接】成功移除 GameplayEffect: 目标=[%s], 扣减层数=%d。"),
            *GetNameSafe(InAbilitySystemComponent->GetOwner()), StacksToRemove);
    }
    return bSuccess;
}

/**
 * @brief 查询指定 Buff 效果是否仍处于激活状态
 * @param InAbilitySystemComponent 目标 ASC
 * @param InHandle 激活效果句柄
 * @return 仍激活返回 true
 */
bool UGameGasBuffBridge::IsBuffEffectActive(UAbilitySystemComponent* InAbilitySystemComponent, FActiveGameplayEffectHandle InHandle) {
    if (!IsValid(InAbilitySystemComponent) || !InHandle.IsValid()) {
        return false;
    }

    return InAbilitySystemComponent->GetActiveGameplayEffect(InHandle) != nullptr;
}

/**
 * @brief 获取指定 Buff 效果的当前叠层数
 * @param InAbilitySystemComponent 目标 ASC
 * @param InHandle 激活效果句柄
 * @return 当前叠层数，未激活返回 0
 */
int32 UGameGasBuffBridge::GetBuffStackCount(UAbilitySystemComponent* InAbilitySystemComponent, FActiveGameplayEffectHandle InHandle) {
    if (!IsValid(InAbilitySystemComponent) || !InHandle.IsValid()) {
        return 0;
    }

    const FActiveGameplayEffect* ActiveGE = InAbilitySystemComponent->GetActiveGameplayEffect(InHandle);
    return ActiveGE != nullptr ? ActiveGE->Spec.GetStackCount() : 0;
}

/**
 * @brief 获取指定 Buff 效果的剩余持续时长（秒）
 * @param InAbilitySystemComponent 目标 ASC
 * @param InHandle 激活效果句柄
 * @return 剩余持续时长，未激活返回 0.0f
 */
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

/**
 * @brief 获取指定 Buff 效果的总持续时长（秒）
 * @param InAbilitySystemComponent 目标 ASC
 * @param InHandle 激活效果句柄
 * @return 总持续时长，无限时长返回 -1.0f，未激活返回 0.0f
 */
float UGameGasBuffBridge::GetBuffDuration(UAbilitySystemComponent* InAbilitySystemComponent, FActiveGameplayEffectHandle InHandle) {
    if (!IsValid(InAbilitySystemComponent) || !InHandle.IsValid()) {
        return 0.0f;
    }

    const FActiveGameplayEffect* ActiveGE = InAbilitySystemComponent->GetActiveGameplayEffect(InHandle);
    return ActiveGE != nullptr ? ActiveGE->GetDuration() : 0.0f;
}
