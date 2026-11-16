/**
 * @file UGameStatComponent.cpp
 * @brief 通用数值管理组件实现
 */

#include "Stat/UGameStatComponent.h"

#include "Stat/FGameStatEffectiveEvaluator.h"

#include "GameFramework/Actor.h"
#include "Logging/GameLogCategories.h"
#include "Net/UnrealNetwork.h"

/**
 * @brief 构造函数：关闭组件 Tick、开启组件网络复制
 */
UGameStatComponent::UGameStatComponent() {
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

/**
 * @brief 网络属性复制规则注册
 * @param OutLifetimeProps 输出的生命周期属性数组
 */
void UGameStatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(UGameStatComponent, ReplicatedStats);
}

/**
 * @brief 从对象池出库激活回调
 */
void UGameStatComponent::OnAcquireFromPool_Implementation() {
}

/**
 * @brief 归还对象池休眠回调：重置所有修饰器并将当前值回满
 */
void UGameStatComponent::OnReleaseToPool_Implementation() {
    ModifierContainer.Reset();
    AcknowledgedDiff.Reset();

    for (FGameStatEntry& Entry : ReplicatedStats) {
        const float OldEffectiveCurrent = GetStatEffectiveCurrent(Entry.Tag);
        const float EffectiveMax = GetStatEffectiveMax(Entry.Tag);
        Entry.Value.Current = Entry.Value.Max;
        BroadcastStatChanged(Entry.Tag, OldEffectiveCurrent, Entry.Value.Current, EffectiveMax);
    }
    GAME_LOG(Log, TEXT("【数值组件】宿主 [%s] 归还对象池，已清空修饰器并回满基础属性。"), *GetNameSafe(GetOwner()));
}

/**
 * @brief 注册（或重设）属性，并立即向既有订阅者广播初始派生值
 * @param InStatTag 属性标签
 * @param InInitialCurrent 初始当前值
 * @param InMax 属性基础上限
 * @param InMin 属性基础下限
 */
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

/**
 * @brief 注销属性并清理索引与已确认值（蓝图轻量门面）
 * @param InStatTag 属性标签
 * @return 属性存在且已成功移除返回 true
 */
bool UGameStatComponent::K2_UnregisterStat(const FGameplayTag& InStatTag) {
    return UnregisterStat(InStatTag).IsSuccess();
}

/**
 * @brief 注销属性并清理索引与已确认值（强类型契约）
 * @param InStatTag 属性标签
 * @return 属性存在且已成功移除返回 Success，未找到返回 Failure(8001)，非权威端返回 Failure(8002)
 */
TGameStatusResult<void> UGameStatComponent::UnregisterStat(const FGameplayTag& InStatTag) {
    if (!InStatTag.IsValid()) {
        return TGameStatusResult<void>::Failure(Game::CommonErrorCodes::InvalidParameter, TEXT("Invalid StatTag"));
    }

    if (!IsAuthoritativeWriteAllowed()) {
        GAME_LOG(Warning, TEXT("【数值组件】非权威端拒绝移除属性: %s"), *InStatTag.ToString());
        return TGameStatusResult<void>::Failure(8002, TEXT("Not authority"));
    }

    const int32 Index = ResolveStatIndex(InStatTag);
    if (Index != INDEX_NONE) {
        ReplicatedStats.RemoveAt(Index);
        AcknowledgedDiff.Remove(InStatTag);
        ModifierContainer.ClearModifiers(InStatTag);
        SubscriptionHub.ClearTagSubscriptions(InStatTag);
        RebuildStatIndex();
        GAME_LOG(Log, TEXT("【数值组件】宿主 [%s] 成功注销属性 [%s]。"),
            *GetNameSafe(GetOwner()), *InStatTag.ToString());
        return TGameStatusResult<void>::Success();
    }

    return TGameStatusResult<void>::Failure(8001, TEXT("Stat not found"));
}

/**
 * @brief 检查属性是否已在组件中注册
 * @param InStatTag 属性标签
 * @return 若已注册返回 true，否则返回 false
 */
bool UGameStatComponent::HasStat(const FGameplayTag& InStatTag) const {
    return FindStat(InStatTag) != nullptr;
}

/**
 * @brief 输出当前已注册的全部属性标签列表
 * @param OutTags 输出标签数组
 */
void UGameStatComponent::GetAllStatTags(TArray<FGameplayTag>& OutTags) const {
    OutTags.Reset();
    OutTags.Reserve(ReplicatedStats.Num());
    for (const FGameStatEntry& Entry : ReplicatedStats) {
        OutTags.Add(Entry.Tag);
    }
}

/**
 * @brief 读取基础当前值（未注册返回 0.0f）
 * @param InStatTag 属性标签
 * @return 基础当前值数值
 */
float UGameStatComponent::GetStatCurrent(const FGameplayTag& InStatTag) const {
    const FGameStatValue* FoundStat = FindStat(InStatTag);
    return FoundStat != nullptr ? FoundStat->Current : 0.0f;
}

/**
 * @brief 读取基础上限（未注册返回 0.0f）
 * @param InStatTag 属性标签
 * @return 基础上限数值
 */
float UGameStatComponent::GetStatMax(const FGameplayTag& InStatTag) const {
    const FGameStatValue* FoundStat = FindStat(InStatTag);
    return FoundStat != nullptr ? FoundStat->Max : 0.0f;
}

/**
 * @brief 获取属性基础下限（未注册返回 0.0f）
 * @param InStatTag 属性标签
 * @return 基础下限数值
 */
float UGameStatComponent::GetStatMin(const FGameplayTag& InStatTag) const {
    const FGameStatValue* FoundStat = FindStat(InStatTag);
    return FoundStat != nullptr ? FoundStat->Min : 0.0f;
}

/**
 * @brief 按基础当前值与基础上限计算百分比（钳制在 0.0 ~ 1.0）
 * @param InStatTag 属性标签
 * @return 百分比浮点值（0.0 ~ 1.0）
 */
float UGameStatComponent::GetStatPercent(const FGameplayTag& InStatTag) const {
    return FGameStatEffectiveEvaluator::EvaluatePercent(GetStatCurrent(InStatTag), GetStatMax(InStatTag), GetStatMin(InStatTag));
}

/**
 * @brief 读取属性的完整基础数值结构体（蓝图轻量门面）
 * @param InStatTag 属性标签
 * @param OutValue 输出的数值结构体
 * @return 属性已注册返回 true，未注册返回 false
 */
bool UGameStatComponent::K2_GetStatValue(const FGameplayTag& InStatTag, FGameStatValue& OutValue) const {
    const TGameStatusResult<FGameStatValue> Result = GetStatValue(InStatTag);
    if (Result.IsSuccess()) {
        OutValue = Result.GetValue();
        return true;
    }
    return false;
}

/**
 * @brief 读取属性的完整基础数值结构体（强类型契约）
 * @param InStatTag 属性标签
 * @return 成功返回数值结构体，未注册返回 Failure(8001)
 */
TGameStatusResult<FGameStatValue> UGameStatComponent::GetStatValue(const FGameplayTag& InStatTag) const {
    const FGameStatValue* FoundStat = FindStat(InStatTag);
    if (FoundStat == nullptr) {
        return TGameStatusResult<FGameStatValue>::Failure(8001, TEXT("Stat not found"));
    }
    return TGameStatusResult<FGameStatValue>::Success(*FoundStat);
}

/**
 * @brief 注册一条属性修正器（委托至内部 ModifierContainer）
 * @param InStatTag 目标属性标签
 * @param InModifier 修正器规则数据
 * @return 成功返回分配的唯一句柄，失败返回 Failure
 */
TGameStatusResult<uint32> UGameStatComponent::AddStatModifier(const FGameplayTag& InStatTag, const FGameStatModifier& InModifier) {
    if (!InStatTag.IsValid()) {
        return TGameStatusResult<uint32>::Failure(Game::CommonErrorCodes::InvalidParameter, TEXT("Invalid StatTag"));
    }

    const bool bStatRegistered = HasStat(InStatTag);
    const float OldEffectiveCurrent = bStatRegistered ? GetStatEffectiveCurrent(InStatTag) : 0.0f;
    const float OldEffectiveMax = bStatRegistered ? GetStatEffectiveMax(InStatTag) : 0.0f;

    const uint32 Handle = ModifierContainer.AddModifier(InStatTag, InModifier);
    NotifyIfEffectiveStatChanged(InStatTag, bStatRegistered, OldEffectiveCurrent, OldEffectiveMax);
    return TGameStatusResult<uint32>::Success(Handle);
}

/**
 * @brief 按句柄注销一条属性修正器
 * @param InStatTag 目标属性标签
 * @param InHandle 修正器句柄
 * @return 成功注销返回 Success，未找到句柄返回 Failure(8004)
 */
TGameStatusResult<void> UGameStatComponent::RemoveStatModifier(const FGameplayTag& InStatTag, uint32 InHandle) {
    const bool bStatRegistered = HasStat(InStatTag);
    const float OldEffectiveCurrent = bStatRegistered ? GetStatEffectiveCurrent(InStatTag) : 0.0f;
    const float OldEffectiveMax = bStatRegistered ? GetStatEffectiveMax(InStatTag) : 0.0f;

    const bool bRemoved = ModifierContainer.RemoveModifier(InStatTag, InHandle);
    if (!bRemoved) {
        return TGameStatusResult<void>::Failure(8004, TEXT("Modifier handle not found"));
    }
    NotifyIfEffectiveStatChanged(InStatTag, bStatRegistered, OldEffectiveCurrent, OldEffectiveMax);
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 清空指定属性下的全部修正器
 * @param InStatTag 目标属性标签
 * @return 被清除的修正器总数
 */
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

/**
 * @brief 读取叠加全部修正器演算后的派生当前值
 * @param InStatTag 目标属性标签
 * @return 计算后的派生当前值
 */
float UGameStatComponent::GetStatEffectiveCurrent(const FGameplayTag& InStatTag) const {
    return FGameStatEffectiveEvaluator::EvaluateCurrent(GetStatCurrent(InStatTag), GetStatMin(InStatTag), InStatTag, ModifierContainer);
}

/**
 * @brief 读取叠加全部修正器演算后的派生上限
 * @param InStatTag 目标属性标签
 * @return 计算后的派生上限
 */
float UGameStatComponent::GetStatEffectiveMax(const FGameplayTag& InStatTag) const {
    return FGameStatEffectiveEvaluator::EvaluateMax(GetStatMax(InStatTag), GetStatMin(InStatTag), InStatTag, ModifierContainer);
}

/**
 * @brief 按派生当前值与派生上限计算派生百分比（0.0 ~ 1.0）
 * @param InStatTag 属性标签
 * @return 派生百分比浮点值
 */
float UGameStatComponent::GetStatEffectivePercent(const FGameplayTag& InStatTag) const {
    return FGameStatEffectiveEvaluator::EvaluatePercent(GetStatEffectiveCurrent(InStatTag), GetStatEffectiveMax(InStatTag), GetStatMin(InStatTag));
}

/**
 * @brief 增减属性当前值（蓝图版本，输出实际生效的增减量）
 * @param InStatTag 属性标签
 * @param Delta 期望增减量
 * @param OutActualDelta 实际生效增量（考虑上下限钳制）
 * @return 基础值确实发生变动返回 true，否则返回 false
 */
bool UGameStatComponent::ModifyStat(const FGameplayTag& InStatTag, float Delta, float& OutActualDelta) {
    OutActualDelta = 0.0f;
    if (FMath::IsNearlyZero(Delta) || !InStatTag.IsValid()) {
        return false;
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

    const float OldEffectiveValue = FGameStatEffectiveEvaluator::EvaluateCurrent(OldValue, FoundStat->Min, InStatTag, ModifierContainer);
    const float NewEffectiveValue = FGameStatEffectiveEvaluator::EvaluateCurrent(TargetValue, FoundStat->Min, InStatTag, ModifierContainer);
    if (!FMath::IsNearlyEqual(OldEffectiveValue, NewEffectiveValue)) {
        BroadcastStatChanged(InStatTag, OldEffectiveValue, NewEffectiveValue, GetStatEffectiveMax(InStatTag));
    }

    GAME_LOG(Log, TEXT("【数值组件】宿主 [%s] 修改属性 [%s]: 增量=%.2f, 基础值: %.2f -> %.2f, 最终有效值: %.2f。"),
        *GetNameSafe(GetOwner()), *InStatTag.ToString(), OutActualDelta, OldValue, TargetValue, NewEffectiveValue);

    return true;
}

/**
 * @brief 直接设置当前值数值
 * @param InStatTag 属性标签
 * @param NewCurrent 新当前值数值
 */
void UGameStatComponent::SetStatCurrent(const FGameplayTag& InStatTag, float NewCurrent) {
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

    const float OldEffectiveValue = FGameStatEffectiveEvaluator::EvaluateCurrent(OldValue, FoundStat->Min, InStatTag, ModifierContainer);
    const float NewEffectiveValue = FGameStatEffectiveEvaluator::EvaluateCurrent(TargetValue, FoundStat->Min, InStatTag, ModifierContainer);
    const float EffectiveMax = GetStatEffectiveMax(InStatTag);
    if (!FMath::IsNearlyEqual(OldEffectiveValue, NewEffectiveValue)) {
        BroadcastStatChanged(InStatTag, OldEffectiveValue, NewEffectiveValue, EffectiveMax);
    }
    GAME_LOG(Log, TEXT("【数值组件】宿主 [%s] 设置属性 [%s] 当前值: %.2f -> %.2f (上限: %.2f)。"),
        *GetNameSafe(GetOwner()), *InStatTag.ToString(), OldValue, TargetValue, EffectiveMax);
}

/**
 * @brief 直接设置属性上限
 * @param InStatTag 属性标签
 * @param NewMax 新上限数值
 * @param bKeepRelativePercent 是否按原有百分比同比例缩放当前值
 */
void UGameStatComponent::SetStatMax(const FGameplayTag& InStatTag, float NewMax, bool bKeepRelativePercent) {
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
    TargetCurrent = FMath::Clamp(TargetCurrent, FoundStat->Min, ClampedMax);

    FoundStat->Max = ClampedMax;
    FoundStat->Current = TargetCurrent;

    const float OldEffectiveValue = FGameStatEffectiveEvaluator::EvaluateCurrent(OldValue, FoundStat->Min, InStatTag, ModifierContainer);
    const float NewEffectiveValue = FGameStatEffectiveEvaluator::EvaluateCurrent(TargetCurrent, FoundStat->Min, InStatTag, ModifierContainer);
    const float EffectiveMax = FGameStatEffectiveEvaluator::EvaluateMax(ClampedMax, FoundStat->Min, InStatTag, ModifierContainer);
    BroadcastStatChanged(InStatTag, OldEffectiveValue, NewEffectiveValue, EffectiveMax);
    GAME_LOG(Log, TEXT("【数值组件】宿主 [%s] 设置属性 [%s] 上限: %.2f -> %.2f (当前值同步为: %.2f)。"),
        *GetNameSafe(GetOwner()), *InStatTag.ToString(), OldMax, ClampedMax, TargetCurrent);
}

/**
 * @brief 订阅单个属性的变动通知（原生委托句柄版本）
 * @param InStatTag 目标属性标签
 * @param InDelegate 订阅回调委托
 * @return 委托句柄，用于后续注销
 */
FDelegateHandle UGameStatComponent::SubscribeStatChanged(const FGameplayTag& InStatTag, FOnGameStatChangedNative::FDelegate InDelegate) {
    const FDelegateHandle Handle = SubscriptionHub.Subscribe(InStatTag, InDelegate);

    if (HasStat(InStatTag)) {
        const float EffectiveCurrent = GetStatEffectiveCurrent(InStatTag);
        const float EffectiveMax = GetStatEffectiveMax(InStatTag);
        InDelegate.ExecuteIfBound(InStatTag, EffectiveCurrent, EffectiveCurrent, EffectiveMax);
    }
    return Handle;
}

/**
 * @brief 订阅单个属性的变动通知，并仅向本次新订阅者回放当前值（绑定 Owner 生命周期）
 * @param Owner 归属的 UObject 对象指针
 * @param InStatTag 目标属性标签
 * @param InCallback 属性变动时的回调闭包
 * @return 委托句柄，用于后续注销
 */
FDelegateHandle UGameStatComponent::SubscribeStatChanged(
    UObject* Owner,
    const FGameplayTag& InStatTag,
    TFunction<void(const FGameplayTag&, float, float, float)> InCallback
) {
    const FDelegateHandle Handle = SubscriptionHub.Subscribe(Owner, InStatTag, InCallback);

    if (Handle.IsValid() && HasStat(InStatTag)) {
        const float EffectiveCurrent = GetStatEffectiveCurrent(InStatTag);
        const float EffectiveMax = GetStatEffectiveMax(InStatTag);
        InCallback(InStatTag, EffectiveCurrent, EffectiveCurrent, EffectiveMax);
    }
    return Handle;
}

/**
 * @brief 按属性标签与句柄取消属性变动订阅
 * @param InStatTag 目标属性标签
 * @param InHandle 注册时返回的委托句柄
 */
void UGameStatComponent::UnsubscribeStatChanged(const FGameplayTag& InStatTag, FDelegateHandle InHandle) {
    SubscriptionHub.Unsubscribe(InStatTag, InHandle);
}

/**
 * @brief 属性数组网络复制到达客户端时的回调
 */
void UGameStatComponent::OnRep_ReplicatedStats() {
    RebuildStatIndex();

    for (const FGameStatEntry& Entry : ReplicatedStats) {
        const float EffectiveCurrent = GetStatEffectiveCurrent(Entry.Tag);
        const float EffectiveMax = GetStatEffectiveMax(Entry.Tag);
        float OldEffectiveCurrent = 0.0f;
        float OldEffectiveMax = 0.0f;
        if (AcknowledgedDiff.NeedsBroadcast(Entry.Tag, EffectiveCurrent, EffectiveMax, OldEffectiveCurrent, OldEffectiveMax)) {
            BroadcastStatChanged(Entry.Tag, OldEffectiveCurrent, EffectiveCurrent, EffectiveMax);
        }
    }

    AcknowledgedDiff.PruneMissing(StatIndex);
}

/**
 * @brief 从标签索引表读取属性所在复制数组下标
 * @param InStatTag 目标属性标签
 * @return 属性下标；未注册时返回 INDEX_NONE
 */
int32 UGameStatComponent::ResolveStatIndex(const FGameplayTag& InStatTag) const {
    if (!InStatTag.IsValid()) {
        return INDEX_NONE;
    }
    const int32* Index = StatIndex.Find(InStatTag);
    return Index != nullptr ? *Index : INDEX_NONE;
}

/**
 * @brief 查找指定标签的可写基础属性值
 * @param InStatTag 目标属性标签
 * @return 可写属性值指针；未注册时返回 nullptr
 */
FGameStatValue* UGameStatComponent::FindMutableStat(const FGameplayTag& InStatTag) {
    const int32 Index = ResolveStatIndex(InStatTag);
    return Index != INDEX_NONE ? &ReplicatedStats[Index].Value : nullptr;
}

/**
 * @brief 查找指定标签的只读基础属性值
 * @param InStatTag 目标属性标签
 * @return 只读属性值指针；未注册时返回 nullptr
 */
const FGameStatValue* UGameStatComponent::FindStat(const FGameplayTag& InStatTag) const {
    const int32 Index = ResolveStatIndex(InStatTag);
    return Index != INDEX_NONE ? &ReplicatedStats[Index].Value : nullptr;
}

/**
 * @brief 更新或新增指定属性的复制条目并同步标签索引
 * @param InStatTag 目标属性标签
 * @param InValue 待写入的基础属性值
 */
void UGameStatComponent::SyncEntryToReplicatedList(const FGameplayTag& InStatTag, const FGameStatValue& InValue) {
    const int32 Index = ResolveStatIndex(InStatTag);
    if (Index != INDEX_NONE) {
        ReplicatedStats[Index].Value = InValue;
        return;
    }

    StatIndex.Add(InStatTag, ReplicatedStats.Num());
    ReplicatedStats.Emplace(InStatTag, InValue);
}

/**
 * @brief 按当前复制数组顺序重建标签到下标的映射
 */
void UGameStatComponent::RebuildStatIndex() {
    StatIndex.Reset();
    StatIndex.Reserve(ReplicatedStats.Num());
    for (int32 Index = 0; Index < ReplicatedStats.Num(); ++Index) {
        StatIndex.Add(ReplicatedStats[Index].Tag, Index);
    }
}

/**
 * @brief 记录已确认有效值并向动态、原生及按标签订阅者广播属性变化
 * @param InStatTag 目标属性标签
 * @param OldValue 变化前的有效当前值
 * @param NewValue 变化后的有效当前值
 * @param MaxValue 当前有效上限
 */
void UGameStatComponent::BroadcastStatChanged(const FGameplayTag& InStatTag, float OldValue, float NewValue, float MaxValue) {
    AcknowledgedDiff.Record(InStatTag, NewValue, MaxValue);

    OnStatChanged.Broadcast(InStatTag, OldValue, NewValue, MaxValue);
    OnStatChangedNative.Broadcast(InStatTag, OldValue, NewValue, MaxValue);
    SubscriptionHub.Broadcast(InStatTag, OldValue, NewValue, MaxValue);
}

/**
 * @brief 修正器操作后重新计算有效值，并仅在当前值或上限变化时广播
 * @param InStatTag 目标属性标签
 * @param bStatRegistered 操作前属性是否已注册
 * @param OldEffectiveCurrent 操作前的有效当前值
 * @param OldEffectiveMax 操作前的有效上限
 */
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

/**
 * @brief 根据宿主网络权限与组件配置判断是否允许写入属性
 * @return 允许写入返回 true，否则返回 false
 */
bool UGameStatComponent::IsAuthoritativeWriteAllowed() const {
    const AActor* OwnerActor = GetOwner();
    return OwnerActor == nullptr || OwnerActor->HasAuthority() || bAllowNonAuthoritativeWrites;
}
