#include "Combat/UGameStatComponent.h"

#include "GameFramework/Actor.h"
#include "Logging/GameLogCategories.h"
#include "Net/UnrealNetwork.h"

namespace {
constexpr uint32 InvalidModifierHandle = 0;
}

UGameStatComponent::UGameStatComponent() {
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UGameStatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(UGameStatComponent, ReplicatedStats);
    DOREPLIFETIME(UGameStatComponent, bVitalAlive);
}

void UGameStatComponent::RegisterStat(const FGameplayTag& InStatTag, float InInitialCurrent, float InMax, float InMin) {
    if (!InStatTag.IsValid()) {
        return;
    }

    if (!IsAuthoritativeWriteAllowed()) {
        GAME_LOG(Warning, TEXT("【数值组件】非权威端拒绝注册属性: %s"), *InStatTag.ToString());
        return;
    }

    const float ClampedMax = FMath::Max(InMax, InMin);
    const float ClampedCurrent = FMath::Clamp(InInitialCurrent, InMin, ClampedMax);

    FGameStatValue NewValue;
    NewValue.Current = ClampedCurrent;
    NewValue.Max = ClampedMax;
    NewValue.Min = InMin;

    SyncEntryToReplicatedList(InStatTag, NewValue);
    AcknowledgedStats.FindOrAdd(InStatTag) = NewValue;
    EvaluateVitalState();
}

bool UGameStatComponent::UnregisterStat(const FGameplayTag& InStatTag) {
    if (!InStatTag.IsValid()) {
        return false;
    }

    if (!IsAuthoritativeWriteAllowed()) {
        GAME_LOG(Warning, TEXT("【数值组件】非权威端拒绝移除属性: %s"), *InStatTag.ToString());
        return false;
    }

    const int32 Index = ResolveStatIndex(InStatTag);
    if (Index != INDEX_NONE) {
        ReplicatedStats.RemoveAt(Index);
        AcknowledgedStats.Remove(InStatTag);
        RebuildStatIndex();
        EvaluateVitalState();
        return true;
    }

    return false;
}

bool UGameStatComponent::HasStat(const FGameplayTag& InStatTag) const {
    return FindStat(InStatTag) != nullptr;
}

void UGameStatComponent::GetAllStatTags(TArray<FGameplayTag>& OutTags) const {
    OutTags.Reset();
    OutTags.Reserve(ReplicatedStats.Num());
    for (const FGameStatEntry& Entry : ReplicatedStats) {
        OutTags.Add(Entry.Tag);
    }
}

float UGameStatComponent::GetStatCurrent(const FGameplayTag& InStatTag) const {
    if (SourceMode == EGameStatSourceMode::Proxy) {
        if (NativeCurrentQuery.IsBound()) {
            return NativeCurrentQuery.Execute(InStatTag);
        }
        if (ProxySource.GetInterface() != nullptr) {
            return IGameStatValueSource::Execute_GetStatCurrent(ProxySource.GetObject(), InStatTag);
        }
    }

    const FGameStatValue* FoundStat = FindStat(InStatTag);
    return FoundStat != nullptr ? FoundStat->Current : 0.0f;
}

float UGameStatComponent::GetStatMax(const FGameplayTag& InStatTag) const {
    if (SourceMode == EGameStatSourceMode::Proxy) {
        if (NativeMaxQuery.IsBound()) {
            return NativeMaxQuery.Execute(InStatTag);
        }
        if (ProxySource.GetInterface() != nullptr) {
            return IGameStatValueSource::Execute_GetStatMax(ProxySource.GetObject(), InStatTag);
        }
    }

    const FGameStatValue* FoundStat = FindStat(InStatTag);
    return FoundStat != nullptr ? FoundStat->Max : 0.0f;
}

float UGameStatComponent::GetStatMin(const FGameplayTag& InStatTag) const {
    const FGameStatValue* FoundStat = FindStat(InStatTag);
    return FoundStat != nullptr ? FoundStat->Min : 0.0f;
}

float UGameStatComponent::GetStatPercent(const FGameplayTag& InStatTag) const {
    const float CurrentVal = GetStatCurrent(InStatTag);
    const float MaxVal = GetStatMax(InStatTag);
    const float MinVal = GetStatMin(InStatTag);

    const float Range = MaxVal - MinVal;
    return (Range > 0.0f) ? FMath::Clamp((CurrentVal - MinVal) / Range, 0.0f, 1.0f) : 0.0f;
}

bool UGameStatComponent::GetStatValue(const FGameplayTag& InStatTag, FGameStatValue& OutValue) const {
    const FGameStatValue* FoundStat = FindStat(InStatTag);
    if (FoundStat == nullptr) {
        return false;
    }

    OutValue = *FoundStat;
    if (SourceMode == EGameStatSourceMode::Proxy) {
        OutValue.Current = GetStatCurrent(InStatTag);
        OutValue.Max = GetStatMax(InStatTag);
    }
    return true;
}

uint32 UGameStatComponent::AddStatModifier(const FGameplayTag& InStatTag, const FGameStatModifier& InModifier) {
    if (!InStatTag.IsValid()) {
        return InvalidModifierHandle;
    }

    const bool bStatRegistered = HasStat(InStatTag);
    const float OldEffectiveCurrent = bStatRegistered ? GetStatEffectiveCurrent(InStatTag) : 0.0f;
    const float OldEffectiveMax = bStatRegistered ? GetStatEffectiveMax(InStatTag) : 0.0f;

    FGameStatModifierEntry Entry;
    Entry.Handle = NextModifierHandle++;
    Entry.Modifier = InModifier;
    StatModifiers.FindOrAdd(InStatTag).Add(Entry);

    NotifyIfEffectiveStatChanged(InStatTag, bStatRegistered, OldEffectiveCurrent, OldEffectiveMax);
    return Entry.Handle;
}

bool UGameStatComponent::RemoveStatModifier(const FGameplayTag& InStatTag, uint32 InHandle) {
    TArray<FGameStatModifierEntry>* Entries = StatModifiers.Find(InStatTag);
    if (Entries == nullptr) {
        return false;
    }

    const bool bStatRegistered = HasStat(InStatTag);
    const float OldEffectiveCurrent = bStatRegistered ? GetStatEffectiveCurrent(InStatTag) : 0.0f;
    const float OldEffectiveMax = bStatRegistered ? GetStatEffectiveMax(InStatTag) : 0.0f;

    const int32 RemovedCount = Entries->RemoveAll([InHandle](const FGameStatModifierEntry& Entry) {
        return Entry.Handle == InHandle;
    });
    if (Entries->IsEmpty()) {
        StatModifiers.Remove(InStatTag);
    }

    if (RemovedCount > 0) {
        NotifyIfEffectiveStatChanged(InStatTag, bStatRegistered, OldEffectiveCurrent, OldEffectiveMax);
    }
    return RemovedCount > 0;
}

int32 UGameStatComponent::ClearStatModifiers(const FGameplayTag& InStatTag) {
    if (!StatModifiers.Contains(InStatTag)) {
        return 0;
    }

    const bool bStatRegistered = HasStat(InStatTag);
    const float OldEffectiveCurrent = bStatRegistered ? GetStatEffectiveCurrent(InStatTag) : 0.0f;
    const float OldEffectiveMax = bStatRegistered ? GetStatEffectiveMax(InStatTag) : 0.0f;

    TArray<FGameStatModifierEntry> Removed;
    if (!StatModifiers.RemoveAndCopyValue(InStatTag, Removed)) {
        return 0;
    }

    NotifyIfEffectiveStatChanged(InStatTag, bStatRegistered, OldEffectiveCurrent, OldEffectiveMax);
    return Removed.Num();
}

float UGameStatComponent::GetStatEffectiveCurrent(const FGameplayTag& InStatTag) const {
    return EvaluateModifiers(InStatTag, GetStatCurrent(InStatTag), false);
}

float UGameStatComponent::GetStatEffectiveMax(const FGameplayTag& InStatTag) const {
    return EvaluateModifiers(InStatTag, GetStatMax(InStatTag), true);
}

float UGameStatComponent::GetStatEffectivePercent(const FGameplayTag& InStatTag) const {
    const float CurrentVal = GetStatEffectiveCurrent(InStatTag);
    const float MaxVal = GetStatEffectiveMax(InStatTag);
    const float MinVal = GetStatMin(InStatTag);

    const float Range = MaxVal - MinVal;
    return (Range > 0.0f) ? FMath::Clamp((CurrentVal - MinVal) / Range, 0.0f, 1.0f) : 0.0f;
}

bool UGameStatComponent::ModifyStat(const FGameplayTag& InStatTag, float Delta, float& OutActualDelta) {
    OutActualDelta = 0.0f;
    if (FMath::IsNearlyZero(Delta) || !InStatTag.IsValid()) {
        return false;
    }

    if (SourceMode == EGameStatSourceMode::Proxy) {
        return ExecuteExternalWrite(InStatTag, EGameStatWriteOp::Modify, Delta, OutActualDelta);
    }

    if (!IsAuthoritativeWriteAllowed()) {
        GAME_LOG(Warning, TEXT("【数值组件】非权威端拒绝修改属性: %s"), *InStatTag.ToString());
        return false;
    }

    FGameStatValue* FoundStat = FindMutableStat(InStatTag);
    if (FoundStat == nullptr) {
        return false;
    }

    const float OldValue = FoundStat->Current;
    const float TargetValue = FMath::Clamp(OldValue + Delta, FoundStat->Min, FoundStat->Max);
    OutActualDelta = TargetValue - OldValue;

    if (FMath::IsNearlyZero(OutActualDelta)) {
        return false;
    }

    FoundStat->Current = TargetValue;

    if (InStatTag.MatchesTagExact(PrimaryVitalTag)) {
        EvaluateVitalState();
    }

    const float OldEffectiveValue = EvaluateModifiers(InStatTag, OldValue, false);
    const float NewEffectiveValue = EvaluateModifiers(InStatTag, TargetValue, false);
    const float EffectiveMax = GetStatEffectiveMax(InStatTag);
    BroadcastStatChanged(InStatTag, OldEffectiveValue, NewEffectiveValue, EffectiveMax);

    return true;
}

void UGameStatComponent::SetStatCurrent(const FGameplayTag& InStatTag, float NewCurrent) {
    if (SourceMode == EGameStatSourceMode::Proxy) {
        float AppliedDelta = 0.0f;
        ExecuteExternalWrite(InStatTag, EGameStatWriteOp::SetCurrent, NewCurrent, AppliedDelta);
        return;
    }

    if (!IsAuthoritativeWriteAllowed()) {
        GAME_LOG(Warning, TEXT("【数值组件】非权威端拒绝设置当前值: %s"), *InStatTag.ToString());
        return;
    }

    FGameStatValue* FoundStat = FindMutableStat(InStatTag);
    if (FoundStat == nullptr) {
        return;
    }

    const float OldValue = FoundStat->Current;
    const float TargetValue = FMath::Clamp(NewCurrent, FoundStat->Min, FoundStat->Max);

    if (FMath::IsNearlyEqual(OldValue, TargetValue)) {
        return;
    }

    FoundStat->Current = TargetValue;

    if (InStatTag.MatchesTagExact(PrimaryVitalTag)) {
        EvaluateVitalState();
    }

    const float OldEffectiveValue = EvaluateModifiers(InStatTag, OldValue, false);
    const float NewEffectiveValue = EvaluateModifiers(InStatTag, TargetValue, false);
    const float EffectiveMax = GetStatEffectiveMax(InStatTag);
    BroadcastStatChanged(InStatTag, OldEffectiveValue, NewEffectiveValue, EffectiveMax);
}

void UGameStatComponent::SetStatMax(const FGameplayTag& InStatTag, float NewMax, bool bKeepRelativePercent) {
    if (SourceMode == EGameStatSourceMode::Proxy) {
        float AppliedDelta = 0.0f;
        ExecuteExternalWrite(InStatTag, EGameStatWriteOp::SetMax, NewMax, AppliedDelta);
        return;
    }

    if (!IsAuthoritativeWriteAllowed()) {
        GAME_LOG(Warning, TEXT("【数值组件】非权威端拒绝设置上限: %s"), *InStatTag.ToString());
        return;
    }

    FGameStatValue* FoundStat = FindMutableStat(InStatTag);
    if (FoundStat == nullptr) {
        return;
    }

    const float ClampedMax = FMath::Max(NewMax, FoundStat->Min);
    const float OldMax = FoundStat->Max;
    const float OldValue = FoundStat->Current;

    if (FMath::IsNearlyEqual(OldMax, ClampedMax)) {
        return;
    }

    float TargetCurrent = OldValue;
    if (bKeepRelativePercent) {
        const float OldRange = OldMax - FoundStat->Min;
        const float NewRange = ClampedMax - FoundStat->Min;
        if (OldRange > 0.0f && NewRange > 0.0f) {
            const float Ratio = (OldValue - FoundStat->Min) / OldRange;
            TargetCurrent = FoundStat->Min + (Ratio * NewRange);
        }
    } else {
        TargetCurrent = FMath::Min(OldValue, ClampedMax);
    }

    FoundStat->Max = ClampedMax;
    FoundStat->Current = TargetCurrent;

    if (InStatTag.MatchesTagExact(PrimaryVitalTag)) {
        EvaluateVitalState();
    }

    const float OldEffectiveValue = EvaluateModifiers(InStatTag, OldValue, false);
    const float NewEffectiveValue = EvaluateModifiers(InStatTag, TargetCurrent, false);
    const float EffectiveMax = EvaluateModifiers(InStatTag, ClampedMax, true);
    BroadcastStatChanged(InStatTag, OldEffectiveValue, NewEffectiveValue, EffectiveMax);
}

void UGameStatComponent::SetPrimaryVitalTag(const FGameplayTag& InTag) {
    PrimaryVitalTag = InTag;
    EvaluateVitalState();
}

bool UGameStatComponent::IsVitalAlive() const noexcept {
    return bVitalAlive;
}

void UGameStatComponent::ReviveVital(float FillPercent) {
    if (!PrimaryVitalTag.IsValid()) {
        return;
    }

    const float MinVal = GetStatMin(PrimaryVitalTag);
    const float MaxVal = GetStatMax(PrimaryVitalTag);
    const float Range = MaxVal - MinVal;
    if (Range <= 0.0f) {
        return;
    }

    const float ClampedPercent = FMath::Clamp(FillPercent, 0.0f, 1.0f);
    const float RevivedValue = MinVal + (Range * ClampedPercent);

    SetStatCurrent(PrimaryVitalTag, RevivedValue);
}

void UGameStatComponent::BindStatSource(TScriptInterface<IGameStatValueSource> InSource) {
    ProxySource = InSource;
    SourceMode = EGameStatSourceMode::Proxy;
}

void UGameStatComponent::BindNativeStatQueries(FGameStatQueryDelegate InCurrentQuery, FGameStatQueryDelegate InMaxQuery) {
    NativeCurrentQuery = InCurrentQuery;
    NativeMaxQuery = InMaxQuery;
    SourceMode = EGameStatSourceMode::Proxy;
}

void UGameStatComponent::BindNativeStatWriter(FGameStatWriteDelegate InWriter) {
    NativeStatWriter = InWriter;
    SourceMode = EGameStatSourceMode::Proxy;
}

void UGameStatComponent::UnbindStatSource() {
    if (SourceMode != EGameStatSourceMode::Proxy) {
        return;
    }

    for (FGameStatEntry& Entry : ReplicatedStats) {
        Entry.Value.Current = GetStatCurrent(Entry.Tag);
        Entry.Value.Max = GetStatMax(Entry.Tag);
    }

    SourceMode = EGameStatSourceMode::Standalone;
    ProxySource = nullptr;
    NativeCurrentQuery.Unbind();
    NativeMaxQuery.Unbind();
    NativeStatWriter.Unbind();

    EvaluateVitalState();
}

bool UGameStatComponent::CanWriteStat() const {
    if (SourceMode == EGameStatSourceMode::Proxy) {
        return NativeStatWriter.IsBound();
    }
    return IsAuthoritativeWriteAllowed();
}

FDelegateHandle UGameStatComponent::SubscribeStatChanged(const FGameplayTag& InStatTag, FOnGameStatChangedNative::FDelegate InDelegate) {
    FOnGameStatChangedNative& Listeners = TaggedStatChanged.FindOrAdd(InStatTag);
    const FDelegateHandle Handle = Listeners.Add(InDelegate);

    if (HasStat(InStatTag)) {
        const float EffectiveCurrent = GetStatEffectiveCurrent(InStatTag);
        const float EffectiveMax = GetStatEffectiveMax(InStatTag);
        InDelegate.ExecuteIfBound(InStatTag, EffectiveCurrent, EffectiveCurrent, EffectiveMax);
    }
    return Handle;
}

FDelegateHandle UGameStatComponent::SubscribeStatChanged(UObject* Owner, const FGameplayTag& InStatTag,
    TFunction<void(const FGameplayTag&, float, float, float)> InCallback) {
    if (!InCallback) {
        return FDelegateHandle();
    }

    const TWeakObjectPtr<UObject> WeakOwner(Owner);
    if (Owner != nullptr && !WeakOwner.IsValid()) {
        return FDelegateHandle();
    }

    PurgeOwnerSubscriptions(InStatTag);
    const FDelegateHandle Handle = SubscribeStatChanged(InStatTag,
        FOnGameStatChangedNative::FDelegate::CreateLambda(
            [WeakOwner, Callback = MoveTemp(InCallback)](const FGameplayTag& Tag, float OldValue, float NewValue, float MaxValue) {
                if (!WeakOwner.IsExplicitlyNull() && !WeakOwner.IsValid()) {
                    return;
                }
                Callback(Tag, OldValue, NewValue, MaxValue);
            }));
    if (Owner != nullptr) {
        OwnerStatSubscriptions.Add(FStatOwnerSubscription{InStatTag, WeakOwner, Handle});
    }
    return Handle;
}

void UGameStatComponent::UnsubscribeStatChanged(const FGameplayTag& InStatTag, FDelegateHandle InHandle) {
    FOnGameStatChangedNative* Listeners = TaggedStatChanged.Find(InStatTag);
    if (Listeners == nullptr) {
        return;
    }

    Listeners->Remove(InHandle);
    OwnerStatSubscriptions.RemoveAll([&InStatTag, InHandle](const FStatOwnerSubscription& Entry) {
        return Entry.Handle == InHandle && Entry.Tag.MatchesTagExact(InStatTag);
    });
    if (!Listeners->IsBound()) {
        TaggedStatChanged.Remove(InStatTag);
    }
}

void UGameStatComponent::OnRep_ReplicatedStats() {
    RebuildStatIndex();

    for (const FGameStatEntry& Entry : ReplicatedStats) {
        const float EffectiveCurrent = GetStatEffectiveCurrent(Entry.Tag);
        const float EffectiveMax = GetStatEffectiveMax(Entry.Tag);
        const FGameStatValue* Previous = AcknowledgedStats.Find(Entry.Tag);
        if (Previous == nullptr) {
            BroadcastStatChanged(Entry.Tag, EffectiveCurrent, EffectiveCurrent, EffectiveMax);
        } else if (!FMath::IsNearlyEqual(Previous->Current, EffectiveCurrent)
            || !FMath::IsNearlyEqual(Previous->Max, EffectiveMax)) {
            BroadcastStatChanged(Entry.Tag, Previous->Current, EffectiveCurrent, EffectiveMax);
        }
    }

    for (auto It = AcknowledgedStats.CreateIterator(); It; ++It) {
        if (!StatIndex.Contains(It.Key())) {
            It.RemoveCurrent();
        }
    }

    EvaluateVitalState();
}

void UGameStatComponent::OnRep_VitalAlive() {
    OnVitalStateChanged.Broadcast(bVitalAlive, PrimaryVitalTag);
    OnVitalStateChangedNative.Broadcast(bVitalAlive, PrimaryVitalTag);
}

int32 UGameStatComponent::ResolveStatIndex(const FGameplayTag& InStatTag) const {
    if (const int32* Index = StatIndex.Find(InStatTag)) {
        if (ReplicatedStats.IsValidIndex(*Index)) {
            return *Index;
        }
    }
    return ReplicatedStats.IndexOfByPredicate([&InStatTag](const FGameStatEntry& Entry) {
        return Entry.Tag.MatchesTagExact(InStatTag);
    });
}

FGameStatValue* UGameStatComponent::FindMutableStat(const FGameplayTag& InStatTag) {
    return const_cast<FGameStatValue*>(const_cast<const UGameStatComponent*>(this)->FindStat(InStatTag));
}

const FGameStatValue* UGameStatComponent::FindStat(const FGameplayTag& InStatTag) const {
    const int32 Index = ResolveStatIndex(InStatTag);
    return Index != INDEX_NONE ? &ReplicatedStats[Index].Value : nullptr;
}

void UGameStatComponent::SyncEntryToReplicatedList(const FGameplayTag& InStatTag, const FGameStatValue& InValue) {
    const int32 Index = ResolveStatIndex(InStatTag);
    if (Index != INDEX_NONE) {
        ReplicatedStats[Index].Value = InValue;
        StatIndex.Add(InStatTag, Index);
        return;
    }

    StatIndex.Add(InStatTag, ReplicatedStats.Num());
    ReplicatedStats.Emplace(InStatTag, InValue);
}

void UGameStatComponent::EvaluateVitalState() {
    if (!PrimaryVitalTag.IsValid()) {
        bVitalAlive = true;
        return;
    }

    const float CurrentVal = GetStatCurrent(PrimaryVitalTag);
    const float MinVal = GetStatMin(PrimaryVitalTag);
    const bool bNewAlive = (CurrentVal > MinVal);

    if (bVitalAlive != bNewAlive) {
        bVitalAlive = bNewAlive;
        OnVitalStateChanged.Broadcast(bVitalAlive, PrimaryVitalTag);
        OnVitalStateChangedNative.Broadcast(bVitalAlive, PrimaryVitalTag);
    }
}

void UGameStatComponent::RebuildStatIndex() {
    StatIndex.Reset();
    StatIndex.Reserve(ReplicatedStats.Num());
    for (int32 Index = 0; Index < ReplicatedStats.Num(); ++Index) {
        StatIndex.Add(ReplicatedStats[Index].Tag, Index);
    }
}

void UGameStatComponent::BroadcastStatChanged(const FGameplayTag& InStatTag, float OldValue, float NewValue, float MaxValue) {
    PurgeOwnerSubscriptions(InStatTag);

    FGameStatValue& Acknowledged = AcknowledgedStats.FindOrAdd(InStatTag);
    Acknowledged.Current = NewValue;
    Acknowledged.Max = MaxValue;

    OnStatChanged.Broadcast(InStatTag, OldValue, NewValue, MaxValue);
    OnStatChangedNative.Broadcast(InStatTag, OldValue, NewValue, MaxValue);
    if (FOnGameStatChangedNative* Listeners = TaggedStatChanged.Find(InStatTag)) {
        Listeners->Broadcast(InStatTag, OldValue, NewValue, MaxValue);
    }
}

void UGameStatComponent::NotifyIfEffectiveStatChanged(const FGameplayTag& InStatTag, bool bStatRegistered, float OldEffectiveCurrent, float OldEffectiveMax) {
    if (!bStatRegistered) {
        return;
    }
    const float NewEffectiveCurrent = GetStatEffectiveCurrent(InStatTag);
    const float NewEffectiveMax = GetStatEffectiveMax(InStatTag);
    if (!FMath::IsNearlyEqual(OldEffectiveCurrent, NewEffectiveCurrent)
        || !FMath::IsNearlyEqual(OldEffectiveMax, NewEffectiveMax)) {
        BroadcastStatChanged(InStatTag, OldEffectiveCurrent, NewEffectiveCurrent, NewEffectiveMax);
    }
}

bool UGameStatComponent::ExecuteExternalWrite(const FGameplayTag& InStatTag, EGameStatWriteOp Op, float InValue, float& OutAppliedDelta) {
    OutAppliedDelta = 0.0f;

    if (!NativeStatWriter.IsBound()) {
        GAME_LOG(Warning, TEXT("【数值组件】代理模式未绑定写委托，拒绝写入属性: %s"), *InStatTag.ToString());
        return false;
    }

    const float PreviousCurrent = GetStatCurrent(InStatTag);
    const float PreviousMax = GetStatMax(InStatTag);
    if (!NativeStatWriter.Execute(InStatTag, Op, InValue, OutAppliedDelta)) {
        OutAppliedDelta = 0.0f;
        return false;
    }

    FGameStatValue* FoundStat = FindMutableStat(InStatTag);
    const float NewCurrent = GetStatCurrent(InStatTag);
    const float NewMax = GetStatMax(InStatTag);
    if (FoundStat != nullptr) {
        FoundStat->Current = NewCurrent;
        FoundStat->Max = NewMax;
    }

    if (InStatTag.MatchesTagExact(PrimaryVitalTag)) {
        EvaluateVitalState();
    }

    const float OldEffectiveValue = EvaluateModifiers(InStatTag, PreviousCurrent, false);
    const float NewEffectiveValue = EvaluateModifiers(InStatTag, NewCurrent, false);
    const float OldEffectiveMax = EvaluateModifiers(InStatTag, PreviousMax, true);
    const float NewEffectiveMax = EvaluateModifiers(InStatTag, NewMax, true);

    if (!FMath::IsNearlyEqual(OldEffectiveValue, NewEffectiveValue) || !FMath::IsNearlyEqual(OldEffectiveMax, NewEffectiveMax)) {
        BroadcastStatChanged(InStatTag, OldEffectiveValue, NewEffectiveValue, NewEffectiveMax);
    }
    return true;
}

bool UGameStatComponent::IsAuthoritativeWriteAllowed() const {
    const AActor* OwnerActor = GetOwner();
    return OwnerActor == nullptr || OwnerActor->HasAuthority() || bAllowNonAuthoritativeWrites;
}

float UGameStatComponent::EvaluateModifiers(const FGameplayTag& InStatTag, float BaseValue, bool bForMax) const {
    const TArray<FGameStatModifierEntry>* Entries = StatModifiers.Find(InStatTag);
    if (Entries == nullptr || Entries->IsEmpty()) {
        return BaseValue;
    }

    float Additive = 0.0f;
    float Multiplier = 1.0f;
    float OverrideValue = 0.0f;
    bool bHasOverride = false;
    bool bAppliedAny = false;

    for (const FGameStatModifierEntry& Entry : *Entries) {
        const bool bShouldApply = bForMax ? Entry.Modifier.bApplyToMax : Entry.Modifier.bApplyToCurrent;
        if (!bShouldApply) {
            continue;
        }
        bAppliedAny = true;
        switch (Entry.Modifier.Op) {
            case EGameStatModifierOp::Add:
                Additive += Entry.Modifier.Value;
                break;
            case EGameStatModifierOp::Multiply:
                Multiplier *= Entry.Modifier.Value;
                break;
            case EGameStatModifierOp::Override:
                OverrideValue = Entry.Modifier.Value;
                bHasOverride = true;
                break;
            default:
                break;
        }
    }

    if (!bAppliedAny) {
        return BaseValue;
    }

    const float Calculated = (BaseValue + Additive) * Multiplier;
    const float FinalVal = bHasOverride ? OverrideValue : Calculated;
    const float MinVal = GetStatMin(InStatTag);
    return FMath::Max(FinalVal, MinVal);
}

void UGameStatComponent::PurgeOwnerSubscriptions(const FGameplayTag& InStatTag) {
    FOnGameStatChangedNative* Listeners = TaggedStatChanged.Find(InStatTag);
    for (int32 Index = OwnerStatSubscriptions.Num() - 1; Index >= 0; --Index) {
        const FStatOwnerSubscription& Entry = OwnerStatSubscriptions[Index];
        if (!Entry.Tag.MatchesTagExact(InStatTag) || Entry.Owner.IsValid()) {
            continue;
        }
        if (Listeners != nullptr) {
            Listeners->Remove(Entry.Handle);
        }
        OwnerStatSubscriptions.RemoveAt(Index);
    }
    if (Listeners != nullptr && !Listeners->IsBound()) {
        TaggedStatChanged.Remove(InStatTag);
    }
}
