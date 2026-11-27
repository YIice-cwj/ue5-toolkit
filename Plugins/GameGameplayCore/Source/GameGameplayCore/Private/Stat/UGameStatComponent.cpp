/**
 * @file UGameStatComponent.cpp
 * @brief 通用数值管理组件实现
 */

#include "Stat/UGameStatComponent.h"

#include "GameFramework/Actor.h"
#include "GameGameplayCoreLog.h"
#include "Net/UnrealNetwork.h"

namespace {

struct FPendingStatBroadcast {
    FGameplayTag Tag;
    float OldCurrent = 0.0f;
    float NewCurrent = 0.0f;
    float NewMax = 0.0f;
};

}  // namespace

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
 * @brief 归还对象池休眠回调：静默清空所有修饰器并将已注册属性回满（不向外广播业务事件）
 */
void UGameStatComponent::OnReleaseToPool_Implementation() {
    ModifierContainer.Reset();
    PendingBatchBaselines.Reset();
    ModifierBatchDepth = 0;
    AcknowledgedDiff.Reset();

    for (FGameStatEntry& Entry : ReplicatedStats) {
        Entry.Value.Current = Entry.Value.Max;
        AcknowledgedDiff.Record(Entry.Tag, Entry.Value.Current, Entry.Value.Max);
    }

    GAME_GAMEPLAY_LOG(
        LogGameStat,
        Verbose,
        TEXT("宿主 [%s] 归还对象池，已静默清空修饰器并回满 %d 项基础属性。"),
        *GetNameSafe(GetOwner()),
        ReplicatedStats.Num()
    );
}

/**
 * @brief 注册（或重设）属性，并立即广播初始或变更后的派生值
 * @param InStatTag 属性标签
 * @param InInitialCurrent 初始当前值（须为有限浮点）
 * @param InMax 属性基础上限（须为有限浮点）
 * @param InMin 属性基础下限（须为有限浮点）
 */
void UGameStatComponent::RegisterStat(const FGameplayTag& InStatTag, float InInitialCurrent, float InMax, float InMin) {
    if (!InStatTag.IsValid() || !FMath::IsFinite(InInitialCurrent) || !FMath::IsFinite(InMax) || !FMath::IsFinite(InMin)) {
        GAME_GAMEPLAY_LOG(
            LogGameStat,
            Warning,
            TEXT("拒绝注册属性：标签无效或包含非有限浮点参数 (Tag=%s)。"),
            *InStatTag.ToString()
        );
        return;
    }

    if (!IsAuthoritativeWriteAllowed()) {
        GAME_GAMEPLAY_LOG_THROTTLE(
            LogGameStat,
            1.0,
            Warning,
            TEXT("非权威端拒绝注册属性: %s"),
            *InStatTag.ToString()
        );
        return;
    }

    const float ClampedMax = FMath::Max(InMax, InMin);
    const float ClampedCurrent = FMath::Clamp(InInitialCurrent, InMin, ClampedMax);
    const FGameStatValue* ExistingStat = FindStat(InStatTag);
    const bool bHadExisting = (ExistingStat != nullptr);
    const float PreviousEffectiveCurrent = bHadExisting
        ? ModifierContainer.EvaluateClamped(InStatTag, ExistingStat->Current, ExistingStat->Min, false)
        : 0.0f;

    FGameStatValue NewValue;
    NewValue.Current = ClampedCurrent;
    NewValue.Max = ClampedMax;
    NewValue.Min = InMin;

    SyncEntryToReplicatedList(InStatTag, NewValue);
    const FGameStatEffectivePair NewEffective = ModifierContainer.EvaluateEffectivePair(InStatTag, NewValue);
    BroadcastStatChanged(
        InStatTag,
        bHadExisting ? PreviousEffectiveCurrent : NewEffective.Current,
        NewEffective.Current,
        NewEffective.Max
    );

    GAME_GAMEPLAY_LOG(
        LogGameStat,
        Verbose,
        TEXT("宿主 [%s] 注册属性 [%s]: 初始值=%.2f, 范围=[%.2f, %.2f]。"),
        *GetNameSafe(GetOwner()),
        *InStatTag.ToString(),
        ClampedCurrent,
        InMin,
        ClampedMax
    );
}

/**
 * @brief 注销属性并清理索引、修正器、订阅者与已确认值（蓝图轻量门面）
 * @param InStatTag 属性标签
 * @return 属性存在且已成功移除返回 true
 */
bool UGameStatComponent::K2_UnregisterStat(const FGameplayTag& InStatTag) {
    return UnregisterStat(InStatTag).IsSuccess();
}

/**
 * @brief 注销属性并清理索引、修正器、订阅者与已确认值（强类型契约）
 * @param InStatTag 属性标签
 * @return 成功返回 Success；参数非法返回 InvalidParameter，非权威返回 NotAuthority，未找到返回 StatNotFound
 */
TGameStatusResult<void> UGameStatComponent::UnregisterStat(const FGameplayTag& InStatTag) {
    if (!InStatTag.IsValid()) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::InvalidParameter,
            TEXT("Invalid StatTag")
        );
    }

    if (!IsAuthoritativeWriteAllowed()) {
        GAME_GAMEPLAY_LOG_THROTTLE(
            LogGameStat,
            1.0,
            Warning,
            TEXT("非权威端拒绝移除属性: %s"),
            *InStatTag.ToString()
        );
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::NotAuthority,
            TEXT("Not authority")
        );
    }

    const int32 Index = ResolveStatIndex(InStatTag);
    if (Index == INDEX_NONE) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::StatNotFound,
            TEXT("Stat not found")
        );
    }

    ReplicatedStats.RemoveAtSwap(Index);
    StatIndex.Remove(InStatTag);
    if (Index < ReplicatedStats.Num()) {
        StatIndex.Add(ReplicatedStats[Index].Tag, Index);
    }

    AcknowledgedDiff.Remove(InStatTag);
    ModifierContainer.ClearModifiers(InStatTag);
    SubscriptionHub.ClearTagSubscriptions(InStatTag);
    PendingBatchBaselines.Remove(FGameFastTagKey(InStatTag));

    GAME_GAMEPLAY_LOG(
        LogGameStat,
        Verbose,
        TEXT("宿主 [%s] 注销属性 [%s]。"),
        *GetNameSafe(GetOwner()),
        *InStatTag.ToString()
    );
    return TGameStatusResult<void>::Success();
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
    const FGameStatValue* FoundStat = FindStat(InStatTag);
    return FoundStat != nullptr ? FoundStat->GetPercent() : 0.0f;
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
 * @return 成功返回数值结构体，未注册返回 StatNotFound
 */
TGameStatusResult<FGameStatValue> UGameStatComponent::GetStatValue(const FGameplayTag& InStatTag) const {
    const FGameStatValue* FoundStat = FindStat(InStatTag);
    if (FoundStat == nullptr) {
        return TGameStatusResult<FGameStatValue>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::StatNotFound,
            TEXT("Stat not found")
        );
    }
    return TGameStatusResult<FGameStatValue>::Success(*FoundStat);
}

/**
 * @brief 注册一条属性修正器（蓝图轻量门面，返回打包后的 64 位非零句柄；失败返回 0）
 * @param InStatTag 目标属性标签
 * @param InModifier 修正器规则数据
 * @return 成功返回非零打包句柄，失败返回 0
 */
int64 UGameStatComponent::K2_AddStatModifier(const FGameplayTag& InStatTag, const FGameStatModifier& InModifier) {
    const TGameStatusResult<FGameStatModifierHandle> Result = AddStatModifier(InStatTag, InModifier);
    return Result.IsSuccess() ? static_cast<int64>(Result.GetValue().PackUInt64()) : 0;
}

/**
 * @brief 注册一条属性修正器（本地派生层，委托至内部 ModifierContainer）
 * @param InStatTag 目标属性标签
 * @param InModifier 修正器规则数据（Value 须为有限浮点）
 * @return 成功返回强类型代际句柄，参数非法返回 InvalidParameter
 */
TGameStatusResult<FGameStatModifierHandle> UGameStatComponent::AddStatModifier(
    const FGameplayTag& InStatTag,
    const FGameStatModifier& InModifier
) {
    if (!InStatTag.IsValid() || !FMath::IsFinite(InModifier.Value)) {
        GAME_GAMEPLAY_LOG(
            LogGameStat,
            Warning,
            TEXT("拒绝注册属性修正器：标签无效或修正值非有限浮点 (Tag=%s)。"),
            *InStatTag.ToString()
        );
        return TGameStatusResult<FGameStatModifierHandle>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::InvalidParameter,
            TEXT("Invalid StatTag or non-finite modifier value")
        );
    }

    const FGameStatValue* FoundStat = FindStat(InStatTag);
    FGameStatEffectivePair OldEffective;
    const bool bHasBaseline = PreparePreModifierBaseline(InStatTag, FoundStat, OldEffective);

    const FGameStatModifierHandle Handle = ModifierContainer.AddModifier(InStatTag, InModifier);
    NotifyAfterModifierMutation(InStatTag, FoundStat, bHasBaseline, OldEffective);
    return TGameStatusResult<FGameStatModifierHandle>::Success(Handle);
}

/**
 * @brief 按打包句柄原地原子更新一条已注册属性修正器（蓝图轻量门面）
 * @param InStatTag 目标属性标签
 * @param InPackedHandle K2_AddStatModifier 返回的打包句柄
 * @param InNewModifier 新的修正器规则数据（Value 须为有限浮点）
 * @return 句柄命中且成功更新返回 true，否则返回 false
 */
bool UGameStatComponent::K2_UpdateStatModifier(
    const FGameplayTag& InStatTag,
    int64 InPackedHandle,
    const FGameStatModifier& InNewModifier
) {
    if (InPackedHandle == 0) {
        return false;
    }
    const FGameStatModifierHandle Handle = FGameStatModifierHandle::FromPackedUInt64(static_cast<uint64>(InPackedHandle));
    return UpdateStatModifier(InStatTag, Handle, InNewModifier).IsSuccess();
}

/**
 * @brief 按句柄原地原子更新一条已注册属性修正器（不触发先删后加的中间态回落广播）
 * @param InStatTag 目标属性标签
 * @param InHandle 修正器代际句柄
 * @param InNewModifier 新的修正器规则数据（Value 须为有限浮点）
 * @return 成功返回 Success；参数非法返回 InvalidParameter，句柄未找到返回 ModifierHandleNotFound
 */
TGameStatusResult<void> UGameStatComponent::UpdateStatModifier(
    const FGameplayTag& InStatTag,
    FGameStatModifierHandle InHandle,
    const FGameStatModifier& InNewModifier
) {
    if (!InStatTag.IsValid() || !FMath::IsFinite(InNewModifier.Value)) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::InvalidParameter,
            TEXT("Invalid StatTag or non-finite modifier value")
        );
    }

    const FGameStatValue* FoundStat = FindStat(InStatTag);
    FGameStatEffectivePair OldEffective;
    const bool bHasBaseline = PreparePreModifierBaseline(InStatTag, FoundStat, OldEffective);

    if (!ModifierContainer.UpdateModifier(InStatTag, InHandle, InNewModifier)) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::ModifierHandleNotFound,
            TEXT("Modifier handle not found")
        );
    }

    NotifyAfterModifierMutation(InStatTag, FoundStat, bHasBaseline, OldEffective);
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 按打包句柄注销一条属性修正器（蓝图轻量门面）
 * @param InStatTag 目标属性标签
 * @param InPackedHandle K2_AddStatModifier 返回的打包句柄
 * @return 成功注销返回 true，否则返回 false
 */
bool UGameStatComponent::K2_RemoveStatModifier(const FGameplayTag& InStatTag, int64 InPackedHandle) {
    if (InPackedHandle == 0) {
        return false;
    }
    const FGameStatModifierHandle Handle = FGameStatModifierHandle::FromPackedUInt64(static_cast<uint64>(InPackedHandle));
    return RemoveStatModifier(InStatTag, Handle).IsSuccess();
}

/**
 * @brief 按代际句柄注销一条属性修正器
 * @param InStatTag 目标属性标签
 * @param InHandle 修正器代际句柄
 * @return 成功注销返回 Success，未找到句柄返回 ModifierHandleNotFound
 */
TGameStatusResult<void> UGameStatComponent::RemoveStatModifier(
    const FGameplayTag& InStatTag,
    FGameStatModifierHandle InHandle
) {
    const FGameStatValue* FoundStat = FindStat(InStatTag);
    FGameStatEffectivePair OldEffective;
    const bool bHasBaseline = PreparePreModifierBaseline(InStatTag, FoundStat, OldEffective);

    if (!ModifierContainer.RemoveModifier(InStatTag, InHandle)) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::ModifierHandleNotFound,
            TEXT("Modifier handle not found")
        );
    }

    NotifyAfterModifierMutation(InStatTag, FoundStat, bHasBaseline, OldEffective);
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 清空指定属性下的全部修正器
 * @param InStatTag 目标属性标签
 * @return 被清除的修正器总数
 */
int32 UGameStatComponent::ClearStatModifiers(const FGameplayTag& InStatTag) {
    const FGameStatValue* FoundStat = FindStat(InStatTag);
    FGameStatEffectivePair OldEffective;
    const bool bHasBaseline = PreparePreModifierBaseline(InStatTag, FoundStat, OldEffective);

    const int32 RemovedCount = ModifierContainer.ClearModifiers(InStatTag);
    if (RemovedCount > 0) {
        NotifyAfterModifierMutation(InStatTag, FoundStat, bHasBaseline, OldEffective);
    }
    return RemovedCount;
}

/**
 * @brief 读取叠加全部修正器演算后的派生当前值
 * @param InStatTag 目标属性标签
 * @return 计算后的派生当前值（未注册返回 0.0f）
 */
float UGameStatComponent::GetStatEffectiveCurrent(const FGameplayTag& InStatTag) const {
    const FGameStatValue* FoundStat = FindStat(InStatTag);
    if (FoundStat == nullptr) {
        return 0.0f;
    }
    return ModifierContainer.EvaluateClamped(InStatTag, FoundStat->Current, FoundStat->Min, false);
}

/**
 * @brief 读取叠加全部修正器演算后的派生上限
 * @param InStatTag 目标属性标签
 * @return 计算后的派生上限（未注册返回 0.0f）
 */
float UGameStatComponent::GetStatEffectiveMax(const FGameplayTag& InStatTag) const {
    const FGameStatValue* FoundStat = FindStat(InStatTag);
    if (FoundStat == nullptr) {
        return 0.0f;
    }
    return ModifierContainer.EvaluateClamped(InStatTag, FoundStat->Max, FoundStat->Min, true);
}

/**
 * @brief 按派生当前值与派生上限计算派生百分比（0.0 ~ 1.0）
 * @param InStatTag 属性标签
 * @return 派生百分比浮点值（未注册返回 0.0f）
 */
float UGameStatComponent::GetStatEffectivePercent(const FGameplayTag& InStatTag) const {
    const FGameStatValue* FoundStat = FindStat(InStatTag);
    if (FoundStat == nullptr) {
        return 0.0f;
    }
    const FGameStatEffectivePair Effective = ModifierContainer.EvaluateEffectivePair(InStatTag, *FoundStat);
    return FGameStatValue::EvaluatePercent(Effective.Current, Effective.Max, FoundStat->Min);
}

/**
 * @brief 增减属性当前值（蓝图版本，输出实际生效的增减量）
 * @param InStatTag 属性标签
 * @param Delta 期望增减量（须为有限浮点）
 * @param OutActualDelta 实际生效增量（考虑上下限钳制）
 * @return 基础值确实发生变动返回 true，否则返回 false
 */
bool UGameStatComponent::ModifyStat(const FGameplayTag& InStatTag, float Delta, float& OutActualDelta) {
    OutActualDelta = 0.0f;
    if (!InStatTag.IsValid() || !FMath::IsFinite(Delta) || FMath::IsNearlyZero(Delta)) {
        if (!FMath::IsFinite(Delta)) {
            GAME_GAMEPLAY_LOG(LogGameStat, Warning, TEXT("拒绝修改属性 [%s]：增量 Delta 为非有限浮点。"), *InStatTag.ToString());
        }
        return false;
    }

    if (!IsAuthoritativeWriteAllowed()) {
        GAME_GAMEPLAY_LOG_THROTTLE(
            LogGameStat,
            1.0,
            Warning,
            TEXT("非权威端拒绝修改属性: %s"),
            *InStatTag.ToString()
        );
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

    const float OldEffectiveValue = ModifierContainer.EvaluateClamped(InStatTag, OldValue, FoundStat->Min, false);
    FoundStat->Current = TargetValue;
    const FGameStatEffectivePair NewEffective = ModifierContainer.EvaluateEffectivePair(InStatTag, *FoundStat);

    if (!FMath::IsNearlyEqual(OldEffectiveValue, NewEffective.Current)) {
        BroadcastStatChanged(InStatTag, OldEffectiveValue, NewEffective.Current, NewEffective.Max);
    }

    GAME_GAMEPLAY_LOG(
        LogGameStat,
        Verbose,
        TEXT("宿主 [%s] 修改属性 [%s]: 增量=%.2f, 基础值: %.2f -> %.2f, 有效值: %.2f。"),
        *GetNameSafe(GetOwner()),
        *InStatTag.ToString(),
        OutActualDelta,
        OldValue,
        TargetValue,
        NewEffective.Current
    );

    return true;
}

/**
 * @brief 直接设置当前值数值
 * @param InStatTag 属性标签
 * @param NewCurrent 新当前值数值（须为有限浮点）
 */
void UGameStatComponent::SetStatCurrent(const FGameplayTag& InStatTag, float NewCurrent) {
    if (!FMath::IsFinite(NewCurrent)) {
        GAME_GAMEPLAY_LOG(LogGameStat, Warning, TEXT("拒绝设置属性 [%s] 当前值：输入为非有限浮点。"), *InStatTag.ToString());
        return;
    }

    if (!IsAuthoritativeWriteAllowed()) {
        GAME_GAMEPLAY_LOG_THROTTLE(
            LogGameStat,
            1.0,
            Warning,
            TEXT("非权威端拒绝设置当前值: %s"),
            *InStatTag.ToString()
        );
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

    const float OldEffectiveValue = ModifierContainer.EvaluateClamped(InStatTag, OldValue, FoundStat->Min, false);
    FoundStat->Current = TargetValue;
    const FGameStatEffectivePair NewEffective = ModifierContainer.EvaluateEffectivePair(InStatTag, *FoundStat);

    if (!FMath::IsNearlyEqual(OldEffectiveValue, NewEffective.Current)) {
        BroadcastStatChanged(InStatTag, OldEffectiveValue, NewEffective.Current, NewEffective.Max);
    }

    GAME_GAMEPLAY_LOG(
        LogGameStat,
        Verbose,
        TEXT("宿主 [%s] 设置属性 [%s] 当前值: %.2f -> %.2f (有效上限: %.2f)。"),
        *GetNameSafe(GetOwner()),
        *InStatTag.ToString(),
        OldValue,
        TargetValue,
        NewEffective.Max
    );
}

/**
 * @brief 直接设置属性上限
 * @param InStatTag 属性标签
 * @param NewMax 新上限数值（须为有限浮点）
 * @param bKeepRelativePercent 是否按原有百分比同比例缩放当前值
 */
void UGameStatComponent::SetStatMax(const FGameplayTag& InStatTag, float NewMax, bool bKeepRelativePercent) {
    if (!FMath::IsFinite(NewMax)) {
        GAME_GAMEPLAY_LOG(LogGameStat, Warning, TEXT("拒绝设置属性 [%s] 上限：输入为非有限浮点。"), *InStatTag.ToString());
        return;
    }

    if (!IsAuthoritativeWriteAllowed()) {
        GAME_GAMEPLAY_LOG_THROTTLE(
            LogGameStat,
            1.0,
            Warning,
            TEXT("非权威端拒绝设置上限: %s"),
            *InStatTag.ToString()
        );
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

    const FGameStatEffectivePair OldEffective = ModifierContainer.EvaluateEffectivePair(InStatTag, *FoundStat);
    FoundStat->Max = ClampedMax;
    FoundStat->Current = TargetCurrent;
    const FGameStatEffectivePair NewEffective = ModifierContainer.EvaluateEffectivePair(InStatTag, *FoundStat);

    if (!FMath::IsNearlyEqual(OldEffective.Current, NewEffective.Current)
        || !FMath::IsNearlyEqual(OldEffective.Max, NewEffective.Max)) {
        BroadcastStatChanged(InStatTag, OldEffective.Current, NewEffective.Current, NewEffective.Max);
    }

    GAME_GAMEPLAY_LOG(
        LogGameStat,
        Verbose,
        TEXT("宿主 [%s] 设置属性 [%s] 上限: %.2f -> %.2f (当前值同步为: %.2f)。"),
        *GetNameSafe(GetOwner()),
        *InStatTag.ToString(),
        OldMax,
        ClampedMax,
        TargetCurrent
    );
}

/**
 * @brief 订阅单个属性的变动通知，并在属性已注册时仅向本次新订阅者立即回放当前有效值
 * @param InStatTag 目标属性标签
 * @param InDelegate 订阅回调委托
 * @return 委托句柄，用于后续注销
 */
FDelegateHandle UGameStatComponent::SubscribeStatChanged(
    const FGameplayTag& InStatTag,
    FOnGameStatChangedNative::FDelegate InDelegate
) {
    const FDelegateHandle Handle = SubscriptionHub.Subscribe(InStatTag, InDelegate);
    if (!Handle.IsValid()) {
        return Handle;
    }

    if (const FGameStatValue* FoundStat = FindStat(InStatTag)) {
        const FGameStatEffectivePair Effective = ModifierContainer.EvaluateEffectivePair(InStatTag, *FoundStat);
        InDelegate.ExecuteIfBound(InStatTag, Effective.Current, Effective.Current, Effective.Max);
    }
    return Handle;
}

/**
 * @brief 订阅单个属性的变动通知，并在属性已注册时仅向本次新订阅者回放当前有效值（绑定 Owner 弱生命周期）
 * @param Owner 归属的 UObject 对象指针（为空时按普通 Lambda 绑定；非空且已失效时拒绝订阅）
 * @param InStatTag 目标属性标签
 * @param InCallback 属性变动时的回调闭包
 * @return 委托句柄，用于后续注销
 */
FDelegateHandle UGameStatComponent::SubscribeStatChanged(
    UObject* Owner,
    const FGameplayTag& InStatTag,
    TFunction<void(const FGameplayTag&, float, float, float)> InCallback
) {
    if (!InStatTag.IsValid() || !InCallback) {
        return FDelegateHandle();
    }
    if (Owner != nullptr && !IsValid(Owner)) {
        return FDelegateHandle();
    }

    FOnGameStatChangedNative::FDelegate Delegate = (Owner != nullptr)
        ? FOnGameStatChangedNative::FDelegate::CreateWeakLambda(Owner, MoveTemp(InCallback))
        : FOnGameStatChangedNative::FDelegate::CreateLambda(MoveTemp(InCallback));
    return SubscribeStatChanged(InStatTag, MoveTemp(Delegate));
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
 * @brief 根据宿主网络权限与组件配置判断是否允许写入属性
 * @return 允许写入返回 true，否则返回 false
 */
bool UGameStatComponent::IsAuthoritativeWriteAllowed() const {
    const AActor* OwnerActor = GetOwner();
    return OwnerActor == nullptr || OwnerActor->HasAuthority() || bAllowNonAuthoritativeWrites;
}

/**
 * @brief 属性数组网络复制到达客户端时的回调
 */
void UGameStatComponent::OnRep_ReplicatedStats() {
    const bool bStructureChanged = !IsStatIndexInSync();
    if (bStructureChanged) {
        RebuildStatIndex();
        AcknowledgedDiff.PruneMissing([this](const FGameplayTag& Tag) {
            return StatIndex.Contains(Tag);
        });
    }

    TArray<FPendingStatBroadcast, TInlineAllocator<8>> PendingBroadcasts;
    for (const FGameStatEntry& Entry : ReplicatedStats) {
        const FGameStatEffectivePair Effective = ModifierContainer.EvaluateEffectivePair(Entry.Tag, Entry.Value);
        float OldEffectiveCurrent = 0.0f;
        float OldEffectiveMax = 0.0f;
        if (AcknowledgedDiff.NeedsBroadcast(Entry.Tag, Effective.Current, Effective.Max, OldEffectiveCurrent, OldEffectiveMax)) {
            AcknowledgedDiff.Record(Entry.Tag, Effective.Current, Effective.Max);
            PendingBroadcasts.Add(FPendingStatBroadcast{Entry.Tag, OldEffectiveCurrent, Effective.Current, Effective.Max});
        }
    }

    for (const FPendingStatBroadcast& Item : PendingBroadcasts) {
        OnStatChanged.Broadcast(Item.Tag, Item.OldCurrent, Item.NewCurrent, Item.NewMax);
        OnStatChangedNative.Broadcast(Item.Tag, Item.OldCurrent, Item.NewCurrent, Item.NewMax);
        SubscriptionHub.Broadcast(Item.Tag, Item.OldCurrent, Item.NewCurrent, Item.NewMax);
    }
}

/**
 * @brief 递增修正器批处理嵌套深度（配合 FScopedModifierBatch 使用）
 */
void UGameStatComponent::BeginModifierBatch() noexcept {
    ++ModifierBatchDepth;
}

/**
 * @brief 递减修正器批处理嵌套深度；退出最外层时按终态差分统一派发属性变更广播
 */
void UGameStatComponent::EndModifierBatch() {
    checkf(ModifierBatchDepth > 0, TEXT("UGameStatComponent::EndModifierBatch called without matching BeginModifierBatch"));
    --ModifierBatchDepth;
    if (ModifierBatchDepth > 0 || PendingBatchBaselines.IsEmpty()) {
        return;
    }

    TSortedVectorMap<FGameFastTagKey, FGameStatEffectivePair, 4> Snapshot = MoveTemp(PendingBatchBaselines);
    PendingBatchBaselines.Reset();

    for (const auto& Entry : Snapshot.GetEntries()) {
        const FGameplayTag& StatTag = Entry.Key.Tag;
        const FGameStatValue* FoundStat = FindStat(StatTag);
        if (FoundStat == nullptr) {
            continue;
        }
        const FGameStatEffectivePair NewEffective = ModifierContainer.EvaluateEffectivePair(StatTag, *FoundStat);
        if (!FMath::IsNearlyEqual(Entry.Value.Current, NewEffective.Current)
            || !FMath::IsNearlyEqual(Entry.Value.Max, NewEffective.Max)) {
            BroadcastStatChanged(StatTag, Entry.Value.Current, NewEffective.Current, NewEffective.Max);
        }
    }
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
 * @brief 检查标签索引表是否仍与复制数组完全对齐
 * @return 索引长度及每个下标标签均一致返回 true
 */
bool UGameStatComponent::IsStatIndexInSync() const {
    if (StatIndex.Num() != ReplicatedStats.Num()) {
        return false;
    }
    for (int32 Index = 0; Index < ReplicatedStats.Num(); ++Index) {
        const int32* MappedIndex = StatIndex.Find(ReplicatedStats[Index].Tag);
        if (MappedIndex == nullptr || *MappedIndex != Index) {
            return false;
        }
    }
    return true;
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
void UGameStatComponent::BroadcastStatChanged(
    const FGameplayTag& InStatTag,
    float OldValue,
    float NewValue,
    float MaxValue
) {
    AcknowledgedDiff.Record(InStatTag, NewValue, MaxValue);

    OnStatChanged.Broadcast(InStatTag, OldValue, NewValue, MaxValue);
    OnStatChangedNative.Broadcast(InStatTag, OldValue, NewValue, MaxValue);
    SubscriptionHub.Broadcast(InStatTag, OldValue, NewValue, MaxValue);
}

/**
 * @brief 静默按代际句柄移除修正器并同步已确认差分值（不广播业务委托，专供对象池入池静默回滚使用）
 * @param InStatTag 目标属性标签
 * @param InHandle 修正器代际句柄
 * @return 成功移除返回 true，句柄未命中返回 false
 */
bool UGameStatComponent::RemoveStatModifierSilent(const FGameplayTag& InStatTag, FGameStatModifierHandle InHandle) {
    if (!ModifierContainer.RemoveModifier(InStatTag, InHandle)) {
        return false;
    }

    if (const FGameStatValue* FoundStat = FindStat(InStatTag)) {
        const FGameStatEffectivePair NewEffective = ModifierContainer.EvaluateEffectivePair(InStatTag, *FoundStat);
        AcknowledgedDiff.Record(InStatTag, NewEffective.Current, NewEffective.Max);
    }
    return true;
}

/**
 * @brief 修正器变更前预采有效基线（仅在属性已注册且批处理尚未记录该标签时演算，不提前写入批处理表）
 * @param InStatTag 目标属性标签
 * @param InFoundStat 已查到的基础属性值指针（可为空）
 * @param OutOldEffective 输出操作前的有效值对
 * @return 本次成功预采了新基线返回 true，无需采样返回 false
 */
bool UGameStatComponent::PreparePreModifierBaseline(
    const FGameplayTag& InStatTag,
    const FGameStatValue* InFoundStat,
    FGameStatEffectivePair& OutOldEffective
) const {
    if (InFoundStat == nullptr) {
        return false;
    }

    if (ModifierBatchDepth > 0 && PendingBatchBaselines.Contains(FGameFastTagKey(InStatTag))) {
        return false;
    }

    OutOldEffective = ModifierContainer.EvaluateEffectivePair(InStatTag, *InFoundStat);
    return true;
}

/**
 * @brief 修正器实际变更成功后提交基线或按需触发差分广播（批处理中仅登记首次成功基线，待退出批处理时统一结算）
 * @param InStatTag 目标属性标签
 * @param InFoundStat 已查到的基础属性值指针（可为空）
 * @param bHasBaseline 本次变更前是否成功采样到新基线
 * @param InOldEffective 操作前的有效值对
 */
void UGameStatComponent::NotifyAfterModifierMutation(
    const FGameplayTag& InStatTag,
    const FGameStatValue* InFoundStat,
    bool bHasBaseline,
    const FGameStatEffectivePair& InOldEffective
) {
    if (InFoundStat == nullptr) {
        return;
    }

    if (ModifierBatchDepth > 0) {
        if (bHasBaseline) {
            PendingBatchBaselines.Add(FGameFastTagKey(InStatTag), InOldEffective);
        }
        return;
    }

    const FGameStatEffectivePair NewEffective = ModifierContainer.EvaluateEffectivePair(InStatTag, *InFoundStat);
    if (!FMath::IsNearlyEqual(InOldEffective.Current, NewEffective.Current)
        || !FMath::IsNearlyEqual(InOldEffective.Max, NewEffective.Max)) {
        BroadcastStatChanged(InStatTag, InOldEffective.Current, NewEffective.Current, NewEffective.Max);
    }
}
