/**
 * @file UGameStatComponent.cpp
 * @brief 通用数值管理组件实现
 */

#include "Stat/UGameStatComponent.h"

#include "GameFramework/Actor.h"
#include "Logging/GameLogCategories.h"
#include "Net/UnrealNetwork.h"

UGameStatComponent::UGameStatComponent() {
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UGameStatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(UGameStatComponent, ReplicatedStats);
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
    const bool bAlreadyRegistered = HasStat(InStatTag);
    const float PreviousEffectiveCurrent = bAlreadyRegistered ? GetStatEffectiveCurrent(InStatTag) : ClampedCurrent;
    const float PreviousEffectiveMax = bAlreadyRegistered ? GetStatEffectiveMax(InStatTag) : ClampedMax;

    FGameStatValue NewValue;
    NewValue.Current = ClampedCurrent;
    NewValue.Max = ClampedMax;
    NewValue.Min = InMin;

    SyncEntryToReplicatedList(InStatTag, NewValue);
    BroadcastStatChanged(InStatTag, PreviousEffectiveCurrent, GetStatEffectiveCurrent(InStatTag), GetStatEffectiveMax(InStatTag));
    GAME_LOG(Log, TEXT("【数值组件】宿主 [%s] 成功注册属性 [%s]: 初始值=%.2f, 范围=[%.2f, %.2f]。"),
        *GetNameSafe(GetOwner()), *InStatTag.ToString(), ClampedCurrent, InMin, ClampedMax);
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
        GAME_LOG(Log, TEXT("【数值组件】宿主 [%s] 成功注销属性 [%s]。"),
            *GetNameSafe(GetOwner()), *InStatTag.ToString());
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
        return 0;
    }

    const bool bStatRegistered = HasStat(InStatTag);
    const float OldEffectiveCurrent = bStatRegistered ? GetStatEffectiveCurrent(InStatTag) : 0.0f;
    const float OldEffectiveMax = bStatRegistered ? GetStatEffectiveMax(InStatTag) : 0.0f;

    const uint32 Handle = ModifierContainer.AddModifier(InStatTag, InModifier);
    NotifyIfEffectiveStatChanged(InStatTag, bStatRegistered, OldEffectiveCurrent, OldEffectiveMax);
    return Handle;
}

bool UGameStatComponent::RemoveStatModifier(const FGameplayTag& InStatTag, uint32 InHandle) {
    const bool bStatRegistered = HasStat(InStatTag);
    const float OldEffectiveCurrent = bStatRegistered ? GetStatEffectiveCurrent(InStatTag) : 0.0f;
    const float OldEffectiveMax = bStatRegistered ? GetStatEffectiveMax(InStatTag) : 0.0f;

    const bool bRemoved = ModifierContainer.RemoveModifier(InStatTag, InHandle);
    if (bRemoved) {
        NotifyIfEffectiveStatChanged(InStatTag, bStatRegistered, OldEffectiveCurrent, OldEffectiveMax);
    }
    return bRemoved;
}

int32 UGameStatComponent::ClearStatModifiers(const FGameplayTag& InStatTag) {
    const bool bStatRegistered = HasStat(InStatTag);
    const float OldEffectiveCurrent = bStatRegistered ? GetStatEffectiveCurrent(InStatTag) : 0.0f;
    const float OldEffectiveMax = bStatRegistered ? GetStatEffectiveMax(InStatTag) : 0.0f;

    const int32 RemovedCount = ModifierContainer.ClearModifiers(InStatTag);
    if (RemovedCount > 0) {
        NotifyIfEffectiveStatChanged(InStatTag, bStatRegistered, OldEffectiveCurrent, OldEffectiveMax);
    }
    return RemovedCount;
}

float UGameStatComponent::GetStatEffectiveCurrent(const FGameplayTag& InStatTag) const {
    const float BaseCurrent = GetStatCurrent(InStatTag);
    if (!ModifierContainer.HasApplicableModifiers(InStatTag, false)) {
        return BaseCurrent;
    }
    const float Evaluated = ModifierContainer.EvaluateModifiers(InStatTag, BaseCurrent, false);
    return FMath::Max(Evaluated, GetStatMin(InStatTag));
}

float UGameStatComponent::GetStatEffectiveMax(const FGameplayTag& InStatTag) const {
    const float BaseMax = GetStatMax(InStatTag);
    if (!ModifierContainer.HasApplicableModifiers(InStatTag, true)) {
        return BaseMax;
    }
    const float Evaluated = ModifierContainer.EvaluateModifiers(InStatTag, BaseMax, true);
    return FMath::Max(Evaluated, GetStatMin(InStatTag));
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

    const float OldEffectiveValue = ModifierContainer.EvaluateModifiers(InStatTag, OldValue, false);
    const float NewEffectiveValue = ModifierContainer.EvaluateModifiers(InStatTag, TargetValue, false);
    if (!FMath::IsNearlyEqual(OldEffectiveValue, NewEffectiveValue)) {
        BroadcastStatChanged(InStatTag, OldEffectiveValue, NewEffectiveValue, GetStatEffectiveMax(InStatTag));
    }

    GAME_LOG(Log, TEXT("【数值组件】宿主 [%s] 修改属性 [%s]: 增量=%.2f, 基础值: %.2f -> %.2f, 最终有效值: %.2f。"),
        *GetNameSafe(GetOwner()), *InStatTag.ToString(), OutActualDelta, OldValue, TargetValue, NewEffectiveValue);

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

    const float OldEffectiveValue = ModifierContainer.EvaluateModifiers(InStatTag, OldValue, false);
    const float NewEffectiveValue = ModifierContainer.EvaluateModifiers(InStatTag, TargetValue, false);
    const float EffectiveMax = GetStatEffectiveMax(InStatTag);
    BroadcastStatChanged(InStatTag, OldEffectiveValue, NewEffectiveValue, EffectiveMax);
    GAME_LOG(Log, TEXT("【数值组件】宿主 [%s] 设置属性 [%s] 当前值: %.2f -> %.2f (上限: %.2f)。"),
        *GetNameSafe(GetOwner()), *InStatTag.ToString(), OldValue, TargetValue, EffectiveMax);
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

    const float OldEffectiveValue = ModifierContainer.EvaluateModifiers(InStatTag, OldValue, false);
    const float NewEffectiveValue = ModifierContainer.EvaluateModifiers(InStatTag, TargetCurrent, false);
    const float EffectiveMax = ModifierContainer.EvaluateModifiers(InStatTag, ClampedMax, true);
    BroadcastStatChanged(InStatTag, OldEffectiveValue, NewEffectiveValue, EffectiveMax);
    GAME_LOG(Log, TEXT("【数值组件】宿主 [%s] 设置属性 [%s] 上限: %.2f -> %.2f (当前值同步为: %.2f)。"),
        *GetNameSafe(GetOwner()), *InStatTag.ToString(), OldMax, ClampedMax, TargetCurrent);
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
}

bool UGameStatComponent::CanWriteStat() const {
    if (SourceMode == EGameStatSourceMode::Proxy) {
        return NativeStatWriter.IsBound();
    }
    return IsAuthoritativeWriteAllowed();
}

FDelegateHandle UGameStatComponent::SubscribeStatChanged(const FGameplayTag& InStatTag, FOnGameStatChangedNative::FDelegate InDelegate) {
    const FDelegateHandle Handle = SubscriptionHub.Subscribe(InStatTag, InDelegate);

    if (HasStat(InStatTag)) {
        const float EffectiveCurrent = GetStatEffectiveCurrent(InStatTag);
        const float EffectiveMax = GetStatEffectiveMax(InStatTag);
        InDelegate.ExecuteIfBound(InStatTag, EffectiveCurrent, EffectiveCurrent, EffectiveMax);
    }
    return Handle;
}

FDelegateHandle UGameStatComponent::SubscribeStatChanged(
    UObject* Owner,
    const FGameplayTag& InStatTag,
    TFunction<void(const FGameplayTag&, float, float, float)> InCallback
) {
    const FDelegateHandle Handle = SubscriptionHub.Subscribe(Owner, InStatTag, MoveTemp(InCallback));

    if (HasStat(InStatTag)) {
        const float EffectiveCurrent = GetStatEffectiveCurrent(InStatTag);
        const float EffectiveMax = GetStatEffectiveMax(InStatTag);
        SubscriptionHub.Broadcast(InStatTag, EffectiveCurrent, EffectiveCurrent, EffectiveMax);
    }
    return Handle;
}

void UGameStatComponent::UnsubscribeStatChanged(const FGameplayTag& InStatTag, FDelegateHandle InHandle) {
    SubscriptionHub.Unsubscribe(InStatTag, InHandle);
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
}

int32 UGameStatComponent::ResolveStatIndex(const FGameplayTag& InStatTag) const {
    const int32* Index = StatIndex.Find(InStatTag);
    return Index != nullptr ? *Index : INDEX_NONE;
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

void UGameStatComponent::RebuildStatIndex() {
    StatIndex.Reset();
    StatIndex.Reserve(ReplicatedStats.Num());
    for (int32 Index = 0; Index < ReplicatedStats.Num(); ++Index) {
        StatIndex.Add(ReplicatedStats[Index].Tag, Index);
    }
}

void UGameStatComponent::BroadcastStatChanged(const FGameplayTag& InStatTag, float OldValue, float NewValue, float MaxValue) {
    FGameStatValue& Acknowledged = AcknowledgedStats.FindOrAdd(InStatTag);
    Acknowledged.Current = NewValue;
    Acknowledged.Max = MaxValue;

    OnStatChanged.Broadcast(InStatTag, OldValue, NewValue, MaxValue);
    OnStatChangedNative.Broadcast(InStatTag, OldValue, NewValue, MaxValue);
    SubscriptionHub.Broadcast(InStatTag, OldValue, NewValue, MaxValue);
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

    const float OldEffectiveValue = ModifierContainer.EvaluateModifiers(InStatTag, PreviousCurrent, false);
    const float NewEffectiveValue = ModifierContainer.EvaluateModifiers(InStatTag, NewCurrent, false);
    const float OldEffectiveMax = ModifierContainer.EvaluateModifiers(InStatTag, PreviousMax, true);
    const float NewEffectiveMax = ModifierContainer.EvaluateModifiers(InStatTag, NewMax, true);

    if (!FMath::IsNearlyEqual(OldEffectiveValue, NewEffectiveValue) || !FMath::IsNearlyEqual(OldEffectiveMax, NewEffectiveMax)) {
        BroadcastStatChanged(InStatTag, OldEffectiveValue, NewEffectiveValue, NewEffectiveMax);
    }
    return true;
}

bool UGameStatComponent::IsAuthoritativeWriteAllowed() const {
    const AActor* OwnerActor = GetOwner();
    return OwnerActor == nullptr || OwnerActor->HasAuthority() || bAllowNonAuthoritativeWrites;
}
