/**
 * @file UGameAbilitySystemComponent.cpp
 * @brief 通用能力系统组件基类实现
 */

#include "Ability/UGameAbilitySystemComponent.h"

#include "AbilitySystemGlobals.h"
#include "GameGameplayLog.h"

namespace {

/**
 * @brief 清空最小复制标签映射并逐项归零以递增 MapID 驱动网络同步移除
 * @param InTagMap 待清空的复制标签映射
 */
void ClearMinimalReplicationTagMap(FMinimalReplicationTagCountMap& InTagMap) {
    if (InTagMap.TagMap.IsEmpty()) {
        return;
    }
    TArray<FGameplayTag> Keys;
    InTagMap.TagMap.GetKeys(Keys);
    for (const FGameplayTag& Tag : Keys) {
        InTagMap.SetTagCount(Tag, 0);
    }
}

}  // namespace

/**
 * @brief 构造函数：开启默认复制，并把 GE 与能力容器注册到自身拥有者
 */
UGameAbilitySystemComponent::UGameAbilitySystemComponent() {
    SetIsReplicatedByDefault(true);
    ActiveGameplayEffects.RegisterWithOwner(this);
    ActivatableAbilities.RegisterWithOwner(this);
}

/**
 * @brief 初始化能力宿主信息（确保 AbilityActorInfo 已分配，交由基类建立宿主关联并记录日志）
 * @param InOwnerActor 拥有者 Actor
 * @param InAvatarActor 附身 Actor
 */
void UGameAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) {
    if (!AbilityActorInfo.IsValid()) {
        AbilityActorInfo = TSharedPtr<FGameplayAbilityActorInfo>(UAbilitySystemGlobals::Get().AllocAbilityActorInfo());
    }
    Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);
    GAME_GAS_LOG(
        LogGameAbility,
        Log,
        TEXT("【GAS 组件】初始化能力 Actor 信息: Owner=[%s], Avatar=[%s]。"),
        *GetNameSafe(InOwnerActor),
        *GetNameSafe(InAvatarActor)
    );
}

/**
 * @brief 从对象池激活回调：确保容器拥有者指针保持绑定当前组件实例
 */
void UGameAbilitySystemComponent::OnAcquireFromPool_Implementation() {
    ActiveGameplayEffects.RegisterWithOwner(this);
    ActivatableAbilities.RegisterWithOwner(this);
}

/**
 * @brief 归还对象池回调：取消并清理权威能力、活跃效果、持续 GameplayCue 与复制标签映射，完整清空显式阻断/松标签与宿主信息
 */
void UGameAbilitySystemComponent::OnReleaseToPool_Implementation() {
    CancelAllAbilities();
    if (IsOwnerActorAuthoritative()) {
        ClearAllAbilities();
        RemoveActiveEffects(FGameplayEffectQuery());
        RemoveAllGameplayCues();
        ClearMinimalReplicationTagMap(GetMinimalReplicationTags_Mutable());
        ClearMinimalReplicationTagMap(GetReplicatedLooseTags_Mutable());
    }

    const FGameplayTagContainer BlockedSnapshot = BlockedAbilityTags.GetExplicitGameplayTags();
    for (const FGameplayTag& BlockedTag : BlockedSnapshot) {
        BlockedAbilityTags.SetTagCount(BlockedTag, 0);
    }

    const FGameplayTagContainer OwnedSnapshot = GetOwnedGameplayTags();
    for (const FGameplayTag& OwnedTag : OwnedSnapshot) {
        SetLooseGameplayTagCount(OwnedTag, 0);
    }
    ResetTagMap();

    if (AbilityActorInfo.IsValid()) {
        ClearActorInfo();
    }
}
