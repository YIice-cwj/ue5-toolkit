/**
 * @file UGameBuffComponent.cpp
 * @brief 通用 Buff / 状态组件实现
 */

#include "Buff/UGameBuffComponent.h"

#include "GameFramework/Actor.h"
#include "Logging/GameLogCategories.h"
#include "Net/UnrealNetwork.h"

UGameBuffComponent::UGameBuffComponent() {
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    SetIsReplicatedByDefault(true);
}

void UGameBuffComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(UGameBuffComponent, ActiveBuffs);
}

void UGameBuffComponent::PostInitProperties() {
    Super::PostInitProperties();
    ActiveBuffs.OwnerComponent = this;
}

void UGameBuffComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (ActiveBuffs.Items.IsEmpty()) {
        return;
    }

    if (!IsApplyAllowed()) {
        bool bHasActiveTimer = false;
        for (FGameBuffInstance& Instance : ActiveBuffs.Items) {
            if (Instance.Spec.Duration > 0.0f) {
                Instance.RemainingSeconds = FMath::Max(0.0f, Instance.RemainingSeconds - DeltaTime);
                if (Instance.RemainingSeconds > 0.0f) {
                    bHasActiveTimer = true;
                }
            }
        }
        if (!bHasActiveTimer) {
            SetComponentTickEnabled(false);
        }
        return;
    }

    TArray<FGameplayTag> ExpiredTags;
    for (int32 Index = ActiveBuffs.Items.Num() - 1; Index >= 0; --Index) {
        FGameBuffInstance& Instance = ActiveBuffs.Items[Index];
        if (Instance.Spec.Duration <= 0.0f) {
            continue;
        }

        Instance.RemainingSeconds -= DeltaTime;
        if (Instance.RemainingSeconds <= 0.0f) {
            GAME_LOG(Log, TEXT("【Buff 组件】宿主 [%s] 的 Buff [%s] (实例 ID: %d) 自然过期注销。"),
                *GetNameSafe(GetOwner()), *Instance.Spec.BuffTag.ToString(), Instance.InstanceId);
            ExpiredTags.AddUnique(Instance.Spec.BuffTag);
            UnregisterInstanceModifiers(Instance);
            ActiveBuffs.Items.RemoveAt(Index);
        }
    }

    if (!ExpiredTags.IsEmpty()) {
        ActiveBuffs.MarkArrayDirty();
        RefreshTickState();
        for (const FGameplayTag& ExpiredTag : ExpiredTags) {
            BroadcastTagStateAfterRemoval(ExpiredTag);
        }
    }
}

void UGameBuffComponent::BindStatComponent(UGameStatComponent* InStatComponent) {
    BoundStatComponent = InStatComponent;
}

int32 UGameBuffComponent::ApplyBuff(const FGameBuffSpec& InSpec, [[maybe_unused]] UObject* InSource) {
    if (!InSpec.BuffTag.IsValid()) {
        GAME_LOG(Warning, TEXT("【Buff 组件】拒绝施加无身份标签的 Buff。"));
        return 0;
    }

    if (!IsApplyAllowed()) {
        GAME_LOG(Warning, TEXT("【Buff 组件】非权威端拒绝施加 Buff: %s"), *InSpec.BuffTag.ToString());
        return 0;
    }

    if (InSpec.StackPolicy == EGameBuffStackPolicy::Independent && InSpec.MaxStacks > 0) {
        if (GetBuffStacks(InSpec.BuffTag) >= InSpec.MaxStacks) {
            GAME_LOG(Warning, TEXT("【Buff 组件】宿主 [%s] 施加 Buff [%s] 失败：已达最大层数 %d。"),
                *GetNameSafe(GetOwner()), *InSpec.BuffTag.ToString(), InSpec.MaxStacks);
            return 0;
        }
    }

    const int32 ExistingIndex = InSpec.StackPolicy != EGameBuffStackPolicy::Independent
        ? FindInstanceIndex(InSpec.BuffTag)
        : INDEX_NONE;

    if (ExistingIndex != INDEX_NONE) {
        FGameBuffInstance& Existing = ActiveBuffs.Items[ExistingIndex];
        UnregisterInstanceModifiers(Existing);

        if (InSpec.MaxStacks <= 0 || Existing.Stacks < InSpec.MaxStacks) {
            ++Existing.Stacks;
        }

        if (InSpec.StackPolicy == EGameBuffStackPolicy::AddDuration) {
            if (InSpec.Duration > 0.0f) {
                Existing.RemainingSeconds += InSpec.Duration;
            }
        } else {
            Existing.RemainingSeconds = InSpec.Duration > 0.0f ? InSpec.Duration : 0.0f;
        }

        Existing.Spec = InSpec;
        RegisterInstanceModifiers(Existing);
        ActiveBuffs.MarkItemDirty(Existing);
        RefreshTickState();
        BroadcastBuffChanged(Existing.Spec.BuffTag, Existing.Stacks);
        GAME_LOG(Log, TEXT("【Buff 组件】宿主 [%s] 叠加 Buff [%s] 成功: 实例 ID=%d, 新层数=%d, 剩余时长=%.2f 秒。"),
            *GetNameSafe(GetOwner()), *Existing.Spec.BuffTag.ToString(), Existing.InstanceId, Existing.Stacks, Existing.RemainingSeconds);
        return Existing.InstanceId;
    }

    FGameBuffInstance& Added = ActiveBuffs.Items.AddDefaulted_GetRef();
    Added.InstanceId = NextInstanceId++;
    Added.Spec = InSpec;
    Added.Stacks = 1;
    Added.RemainingSeconds = InSpec.Duration > 0.0f ? InSpec.Duration : 0.0f;

    RegisterInstanceModifiers(Added);
    ActiveBuffs.MarkItemDirty(Added);
    RefreshTickState();
    BroadcastBuffChanged(Added.Spec.BuffTag, GetBuffStacks(Added.Spec.BuffTag));
    GAME_LOG(Log, TEXT("【Buff 组件】宿主 [%s] 施加 Buff [%s] 成功: 实例 ID=%d, 层数=%d, 持续时间=%.2f 秒。"),
        *GetNameSafe(GetOwner()), *Added.Spec.BuffTag.ToString(), Added.InstanceId, Added.Stacks, Added.RemainingSeconds);
    return Added.InstanceId;
}

bool UGameBuffComponent::RemoveBuff(FGameplayTag InBuffTag, bool bRemoveAllInstances) {
    if (!InBuffTag.IsValid()) {
        return false;
    }

    if (!IsApplyAllowed()) {
        GAME_LOG(Warning, TEXT("【Buff 组件】非权威端拒绝移除 Buff: %s"), *InBuffTag.ToString());
        return false;
    }

    bool bRemoved = false;
    for (int32 Index = ActiveBuffs.Items.Num() - 1; Index >= 0; --Index) {
        if (!ActiveBuffs.Items[Index].Spec.BuffTag.MatchesTagExact(InBuffTag)) {
            continue;
        }

        UnregisterInstanceModifiers(ActiveBuffs.Items[Index]);
        ActiveBuffs.Items.RemoveAt(Index);
        bRemoved = true;
        if (!bRemoveAllInstances) {
            break;
        }
    }

    if (bRemoved) {
        ActiveBuffs.MarkArrayDirty();
        RefreshTickState();
        BroadcastTagStateAfterRemoval(InBuffTag);
        GAME_LOG(Log, TEXT("【Buff 组件】宿主 [%s] 移除 Buff [%s] 成功 (全实例移除=%s)。"),
            *GetNameSafe(GetOwner()), *InBuffTag.ToString(), bRemoveAllInstances ? TEXT("True") : TEXT("False"));
    }
    return bRemoved;
}

bool UGameBuffComponent::RemoveBuffStacks(FGameplayTag InBuffTag, int32 StacksToRemove) {
    if (!InBuffTag.IsValid() || StacksToRemove <= 0) {
        return false;
    }

    if (!IsApplyAllowed()) {
        GAME_LOG(Warning, TEXT("【Buff 组件】非权威端拒绝扣减 Buff 层数: %s"), *InBuffTag.ToString());
        return false;
    }

    int32 RemainingToRemove = StacksToRemove;
    bool bChanged = false;

    for (int32 Index = ActiveBuffs.Items.Num() - 1; Index >= 0 && RemainingToRemove > 0; --Index) {
        FGameBuffInstance& Instance = ActiveBuffs.Items[Index];
        if (!Instance.Spec.BuffTag.MatchesTagExact(InBuffTag)) {
            continue;
        }

        bChanged = true;
        if (Instance.Stacks <= RemainingToRemove) {
            RemainingToRemove -= Instance.Stacks;
            UnregisterInstanceModifiers(Instance);
            ActiveBuffs.Items.RemoveAt(Index);
        } else {
            Instance.Stacks -= RemainingToRemove;
            RemainingToRemove = 0;
            UnregisterInstanceModifiers(Instance);
            RegisterInstanceModifiers(Instance);
            ActiveBuffs.MarkItemDirty(Instance);
        }
    }

    if (bChanged) {
        ActiveBuffs.MarkArrayDirty();
        RefreshTickState();
        BroadcastTagStateAfterRemoval(InBuffTag);
        GAME_LOG(Log, TEXT("【Buff 组件】宿主 [%s] 扣减 Buff [%s] 层数成功 (请求扣减: %d)。"),
            *GetNameSafe(GetOwner()), *InBuffTag.ToString(), StacksToRemove);
    }

    return bChanged;
}

bool UGameBuffComponent::RemoveBuffInstance(int32 InInstanceId) {
    if (!IsApplyAllowed()) {
        GAME_LOG(Warning, TEXT("【Buff 组件】非权威端拒绝移除 Buff 实例: %d"), InInstanceId);
        return false;
    }

    for (int32 Index = 0; Index < ActiveBuffs.Items.Num(); ++Index) {
        if (ActiveBuffs.Items[Index].InstanceId != InInstanceId) {
            continue;
        }

        const FGameplayTag RemovedTag = ActiveBuffs.Items[Index].Spec.BuffTag;
        UnregisterInstanceModifiers(ActiveBuffs.Items[Index]);
        ActiveBuffs.Items.RemoveAt(Index);
        ActiveBuffs.MarkArrayDirty();
        RefreshTickState();
        BroadcastTagStateAfterRemoval(RemovedTag);
        GAME_LOG(Log, TEXT("【Buff 组件】宿主 [%s] 移除 Buff 实例成功: ID=%d, 标签=[%s]。"),
            *GetNameSafe(GetOwner()), InInstanceId, *RemovedTag.ToString());
        return true;
    }
    return false;
}

bool UGameBuffComponent::HasBuff(FGameplayTag InBuffTag) const {
    return FindInstanceIndex(InBuffTag) != INDEX_NONE;
}

int32 UGameBuffComponent::GetBuffStacks(FGameplayTag InBuffTag) const {
    int32 TotalStacks = 0;
    for (const FGameBuffInstance& Instance : ActiveBuffs.Items) {
        if (Instance.Spec.BuffTag.MatchesTagExact(InBuffTag)) {
            TotalStacks += Instance.Stacks;
        }
    }
    return TotalStacks;
}

float UGameBuffComponent::GetBuffRemainingTime(FGameplayTag InBuffTag) const {
    float Remaining = 0.0f;
    for (const FGameBuffInstance& Instance : ActiveBuffs.Items) {
        if (Instance.Spec.BuffTag.MatchesTagExact(InBuffTag)) {
            Remaining = FMath::Max(Remaining, Instance.RemainingSeconds);
        }
    }
    return Remaining;
}

void UGameBuffComponent::GetActiveBuffTags(TArray<FGameplayTag>& OutTags) const {
    OutTags.Reset();
    for (const FGameBuffInstance& Instance : ActiveBuffs.Items) {
        OutTags.AddUnique(Instance.Spec.BuffTag);
    }
}

void UGameBuffComponent::GetGrantedTags(FGameplayTagContainer& OutTags) const {
    OutTags.Reset();
    for (const FGameBuffInstance& Instance : ActiveBuffs.Items) {
        OutTags.AppendTags(Instance.Spec.GrantedTags);
    }
}

bool UGameBuffComponent::IsApplyAllowed() const {
    const AActor* OwnerActor = GetOwner();
    return OwnerActor == nullptr || OwnerActor->HasAuthority() || !bRequireAuthority;
}

UGameStatComponent* UGameBuffComponent::ResolveStatComponent() {
    if (BoundStatComponent.IsValid()) {
        return BoundStatComponent.Get();
    }

    if (const AActor* OwnerActor = GetOwner()) {
        UGameStatComponent* FoundComponent = OwnerActor->FindComponentByClass<UGameStatComponent>();
        BoundStatComponent = FoundComponent;
        return FoundComponent;
    }
    return nullptr;
}

void UGameBuffComponent::RegisterInstanceModifiers(FGameBuffInstance& InInstance) {
    UGameStatComponent* StatComponent = ResolveStatComponent();
    if (StatComponent == nullptr || InInstance.Spec.Modifiers.IsEmpty()) {
        return;
    }

    TArray<FGameBuffAppliedModifier>& Handles = ModifierHandlesByInstance.FindOrAdd(InInstance.InstanceId);
    for (const FGameStatModifierSpec& ModifierSpec : InInstance.Spec.Modifiers) {
        FGameStatModifier ScaledModifier = ModifierSpec.Modifier;
        if (InInstance.Stacks > 1) {
            if (ScaledModifier.Op == EGameStatModifierOp::Add) {
                ScaledModifier.Value *= static_cast<float>(InInstance.Stacks);
            } else if (ScaledModifier.Op == EGameStatModifierOp::Multiply) {
                ScaledModifier.Value = 1.0f + (ScaledModifier.Value - 1.0f) * static_cast<float>(InInstance.Stacks);
            }
        }

        FGameBuffAppliedModifier Applied;
        Applied.StatTag = ModifierSpec.StatTag;
        Applied.Handle = StatComponent->AddStatModifier(ModifierSpec.StatTag, ScaledModifier);
        Handles.Add(Applied);
    }
}

void UGameBuffComponent::UnregisterInstanceModifiers(const FGameBuffInstance& InInstance) {
    TArray<FGameBuffAppliedModifier> Handles;
    if (!ModifierHandlesByInstance.RemoveAndCopyValue(InInstance.InstanceId, Handles)) {
        return;
    }

    UGameStatComponent* StatComponent = ResolveStatComponent();
    if (StatComponent == nullptr) {
        return;
    }

    for (const FGameBuffAppliedModifier& Applied : Handles) {
        StatComponent->RemoveStatModifier(Applied.StatTag, Applied.Handle);
    }
}

int32 UGameBuffComponent::FindInstanceIndex(const FGameplayTag& InBuffTag) const {
    for (int32 Index = 0; Index < ActiveBuffs.Items.Num(); ++Index) {
        if (ActiveBuffs.Items[Index].Spec.BuffTag.MatchesTagExact(InBuffTag)) {
            return Index;
        }
    }
    return INDEX_NONE;
}

void UGameBuffComponent::RefreshTickState() {
    bool bHasTimedInstance = false;
    for (const FGameBuffInstance& Instance : ActiveBuffs.Items) {
        if (Instance.Spec.Duration > 0.0f && Instance.RemainingSeconds > 0.0f) {
            bHasTimedInstance = true;
            break;
        }
    }
    SetComponentTickEnabled(bHasTimedInstance);
}

void UGameBuffComponent::PublishReplicatedDiff(const TArray<FGameBuffInstance>& InItems, TArrayView<const int32> ExcludedIndices) {
    TMap<FGameplayTag, int32> CurrentStacks;
    for (int32 Index = 0; Index < InItems.Num(); ++Index) {
        if (ExcludedIndices.Contains(Index)) {
            continue;
        }

        const FGameBuffInstance& Instance = InItems[Index];
        CurrentStacks.FindOrAdd(Instance.Spec.BuffTag) += Instance.Stacks;
    }

    TArray<FGameplayTag> RemovedTags;
    for (const TPair<FGameplayTag, int32>& Pair : AcknowledgedStacks) {
        if (!CurrentStacks.Contains(Pair.Key)) {
            RemovedTags.Add(Pair.Key);
        }
    }
    for (const FGameplayTag& RemovedTag : RemovedTags) {
        BroadcastBuffRemoved(RemovedTag);
    }

    for (const TPair<FGameplayTag, int32>& Pair : CurrentStacks) {
        const int32* PreviousStacks = AcknowledgedStacks.Find(Pair.Key);
        if (PreviousStacks == nullptr || *PreviousStacks != Pair.Value) {
            BroadcastBuffChanged(Pair.Key, Pair.Value);
        }
    }
}

void UGameBuffComponent::BroadcastTagStateAfterRemoval(const FGameplayTag& InBuffTag) {
    if (FindInstanceIndex(InBuffTag) != INDEX_NONE) {
        BroadcastBuffChanged(InBuffTag, GetBuffStacks(InBuffTag));
    } else {
        BroadcastBuffRemoved(InBuffTag);
    }
}

void UGameBuffComponent::BroadcastBuffChanged(const FGameplayTag& InBuffTag, int32 InStacks) {
    AcknowledgedStacks.FindOrAdd(InBuffTag) = InStacks;
    OnBuffChanged.Broadcast(InBuffTag, InStacks);
    OnBuffChangedNative.Broadcast(InBuffTag, InStacks);
}

void UGameBuffComponent::BroadcastBuffRemoved(const FGameplayTag& InBuffTag) {
    AcknowledgedStacks.Remove(InBuffTag);
    OnBuffRemoved.Broadcast(InBuffTag);
    OnBuffRemovedNative.Broadcast(InBuffTag);
}

