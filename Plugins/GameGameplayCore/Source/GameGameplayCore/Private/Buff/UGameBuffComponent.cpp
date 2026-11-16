/**
 * @file UGameBuffComponent.cpp
 * @brief 通用 Buff / 状态组件实现
 */

#include "Buff/UGameBuffComponent.h"

#include "Stat/UGameStatComponent.h"
#include "GameFramework/Actor.h"
#include "Logging/GameLogCategories.h"
#include "Net/UnrealNetwork.h"

/**
 * @brief 构造函数：启用 Tick（默认关闭）、开启组件网络复制
 */
UGameBuffComponent::UGameBuffComponent() {
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    SetIsReplicatedByDefault(true);
}

/**
 * @brief 每帧推进到期计时：权威端移除到期实例并回滚修正器，非权威端仅执行本地倒计时
 * @param DeltaTime 帧时间间隔（秒）
 * @param TickType Tick 类型
 * @param ThisTickFunction Tick 函数指针
 */
void UGameBuffComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (ActiveBuffs.Items.IsEmpty()) {
        SetComponentTickEnabled(false);
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

/**
 * @brief 网络属性复制规则注册
 * @param OutLifetimeProps 输出的生命周期属性数组
 */
void UGameBuffComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(UGameBuffComponent, ActiveBuffs);
}

/**
 * @brief 属性初始化后生命周期回调：初始化 FastArray 对本组件的反向引用
 */
void UGameBuffComponent::PostInitProperties() {
    Super::PostInitProperties();
    ActiveBuffs.OwnerComponent = this;
}

/**
 * @brief 组件注册生命周期回调：确保 FastArray 反向引用指针始终绑定当前组件实例
 */
void UGameBuffComponent::OnRegister() {
    Super::OnRegister();
    ActiveBuffs.OwnerComponent = this;
}

/**
 * @brief 组件结束运行生命周期回调：从原数值组件回滚全部 Buff 修正器
 * @param EndPlayReason 结束运行的原因
 */
void UGameBuffComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    if (UGameStatComponent* StatComponent = BoundStatComponent.Get()) {
        ModifierBridge.ClearAll(*StatComponent);
    } else {
        ModifierBridge.Reset();
    }

    Super::EndPlay(EndPlayReason);
}

/**
 * @brief 从对象池出库激活回调
 */
void UGameBuffComponent::OnAcquireFromPool_Implementation() {
    RefreshTickState();
}

/**
 * @brief 归还对象池休眠回调：清空所有 Buff 实例并回滚修正器
 */
void UGameBuffComponent::OnReleaseToPool_Implementation() {
    if (UGameStatComponent* StatComponent = BoundStatComponent.Get()) {
        ModifierBridge.ClearAll(*StatComponent);
    } else {
        ModifierBridge.Reset();
    }

    ActiveBuffs.Items.Reset();
    ActiveBuffs.MarkArrayDirty();
    SetComponentTickEnabled(false);
    NextInstanceId = 1;
    AcknowledgedStacks.Reset();
    GAME_LOG(Log, TEXT("【Buff 组件】宿主 [%s] 归还对象池，已清空并重置全部 Buff 状态。"), *GetNameSafe(GetOwner()));
}

/**
 * @brief 显式绑定数值组件，回滚旧组件修正器并将全部活跃 Buff 迁移至新组件
 * @param InStatComponent 目标数值组件指针；为空时仅解绑并回滚
 */
void UGameBuffComponent::BindStatComponent(UGameStatComponent* InStatComponent) {
    if (BoundStatComponent.Get() == InStatComponent) {
        return;
    }

    if (UGameStatComponent* OldStatComponent = BoundStatComponent.Get()) {
        ModifierBridge.ClearAll(*OldStatComponent);
    } else {
        ModifierBridge.Reset();
    }

    BoundStatComponent = InStatComponent;
    if (InStatComponent == nullptr) {
        return;
    }

    for (const FGameBuffInstance& Instance : ActiveBuffs.Items) {
        ModifierBridge.Register(Instance.InstanceId, Instance, *InStatComponent);
    }
}

/**
 * @brief 施加 Buff（蓝图轻量门面）
 * @param InSpec Buff 完整描述配置
 * @return 成功生成或刷新的运行实例 ID；若操作被拒绝则返回 0
 */
int32 UGameBuffComponent::K2_ApplyBuff(const FGameBuffSpec& InSpec) {
    const TGameStatusResult<int32> Result = ApplyBuff(InSpec);
    return Result.IsSuccess() ? Result.GetValue() : 0;
}

/**
 * @brief 施加 Buff（强类型契约）
 * @param InSpec Buff 完整描述配置
 * @return 成功返回实例 ID，非权威返回 Failure(8002)，参数非法/达上限返回 Failure(8003)
 */
TGameStatusResult<int32> UGameBuffComponent::ApplyBuff(const FGameBuffSpec& InSpec) {
    if (!InSpec.BuffTag.IsValid()) {
        GAME_LOG(Warning, TEXT("【Buff 组件】拒绝施加无身份标签的 Buff。"));
        return TGameStatusResult<int32>::Failure(8003, TEXT("Invalid BuffTag"));
    }

    if (!IsApplyAllowed()) {
        GAME_LOG(Warning, TEXT("【Buff 组件】非权威端拒绝施加 Buff: %s"), *InSpec.BuffTag.ToString());
        return TGameStatusResult<int32>::Failure(8002, TEXT("Not authority"));
    }

    if (InSpec.StackPolicy == EGameBuffStackPolicy::Independent && InSpec.MaxStacks > 0) {
        if (GetBuffStacks(InSpec.BuffTag) >= InSpec.MaxStacks) {
            GAME_LOG(Warning, TEXT("【Buff 组件】宿主 [%s] 施加 Buff [%s] 失败：已达最大层数 %d。"),
                *GetNameSafe(GetOwner()), *InSpec.BuffTag.ToString(), InSpec.MaxStacks);
            return TGameStatusResult<int32>::Failure(8003, TEXT("Max buff stacks reached"));
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
            } else {
                Existing.RemainingSeconds = 0.0f;
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
        return TGameStatusResult<int32>::Success(Existing.InstanceId);
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
    return TGameStatusResult<int32>::Success(Added.InstanceId);
}

/**
 * @brief 按 Buff 身份标签移除实例（蓝图轻量门面）
 * @param InBuffTag 目标 Buff 身份标签
 * @param bRemoveAllInstances 是否移除该标签下的全部实例（若为 false 则仅移除一个实例）
 * @return 成功移除至少一个实例返回 true，未找到匹配实例返回 false
 */
bool UGameBuffComponent::K2_RemoveBuff(FGameplayTag InBuffTag, bool bRemoveAllInstances) {
    return RemoveBuff(InBuffTag, bRemoveAllInstances).IsSuccess();
}

/**
 * @brief 按 Buff 身份标签移除实例（强类型契约）
 * @param InBuffTag 目标 Buff 身份标签
 * @param bRemoveAllInstances 是否移除该标签下的全部实例（若为 false 则仅移除一个实例）
 * @return 成功移除返回 Success，非权威返回 Failure(8002)，未找到返回 Failure(8004)
 */
TGameStatusResult<void> UGameBuffComponent::RemoveBuff(FGameplayTag InBuffTag, bool bRemoveAllInstances) {
    if (!InBuffTag.IsValid()) {
        return TGameStatusResult<void>::Failure(8003, TEXT("Invalid BuffTag"));
    }

    if (!IsApplyAllowed()) {
        GAME_LOG(Warning, TEXT("【Buff 组件】非权威端拒绝移除 Buff: %s"), *InBuffTag.ToString());
        return TGameStatusResult<void>::Failure(8002, TEXT("Not authority"));
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
        return TGameStatusResult<void>::Success();
    }
    return TGameStatusResult<void>::Failure(8004, TEXT("Buff not found"));
}

/**
 * @brief 按 Buff 身份标签扣减指定层数（蓝图轻量门面）
 * @param InBuffTag 目标 Buff 身份标签
 * @param StacksToRemove 扣减层数（默认 1 层）
 * @return 成功扣减返回 true，实例不存在或扣减失败返回 false
 */
bool UGameBuffComponent::K2_RemoveBuffStacks(FGameplayTag InBuffTag, int32 StacksToRemove) {
    return RemoveBuffStacks(InBuffTag, StacksToRemove).IsSuccess();
}

/**
 * @brief 按 Buff 身份标签扣减指定层数（强类型契约）
 * @param InBuffTag 目标 Buff 身份标签
 * @param StacksToRemove 扣减层数（默认 1 层）
 * @return 成功扣减返回 Success，参数非法返回 Failure(8003)，非权威端返回 Failure(8002)，未找到返回 Failure(8004)
 */
TGameStatusResult<void> UGameBuffComponent::RemoveBuffStacks(FGameplayTag InBuffTag, int32 StacksToRemove) {
    if (!InBuffTag.IsValid() || StacksToRemove <= 0) {
        return TGameStatusResult<void>::Failure(8003, TEXT("Invalid parameter"));
    }

    if (!IsApplyAllowed()) {
        GAME_LOG(Warning, TEXT("【Buff 组件】非权威端拒绝扣减 Buff 层数: %s"), *InBuffTag.ToString());
        return TGameStatusResult<void>::Failure(8002, TEXT("Not authority"));
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
        return TGameStatusResult<void>::Success();
    }

    return TGameStatusResult<void>::Failure(8004, TEXT("Buff not found"));
}

/**
 * @brief 按运行实例 ID 精确移除单个 Buff 实例（蓝图轻量门面）
 * @param InInstanceId 运行实例唯一 ID
 * @return 成功移除返回 true，未找到匹配实例返回 false
 */
bool UGameBuffComponent::K2_RemoveBuffInstance(int32 InInstanceId) {
    return RemoveBuffInstance(InInstanceId).IsSuccess();
}

/**
 * @brief 按运行实例 ID 精确移除单个 Buff 实例（强类型契约）
 * @param InInstanceId 运行实例唯一 ID
 * @return 成功移除返回 Success，非权威端返回 Failure(8002)，未找到返回 Failure(8004)
 */
TGameStatusResult<void> UGameBuffComponent::RemoveBuffInstance(int32 InInstanceId) {
    if (!IsApplyAllowed()) {
        GAME_LOG(Warning, TEXT("【Buff 组件】非权威端拒绝移除 Buff 实例: %d"), InInstanceId);
        return TGameStatusResult<void>::Failure(8002, TEXT("Not authority"));
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
        return TGameStatusResult<void>::Success();
    }
    return TGameStatusResult<void>::Failure(8004, TEXT("Buff instance not found"));
}

/**
 * @brief 检查是否持有指定身份标签的 Buff
 * @param InBuffTag 目标 Buff 身份标签
 * @return 若存在至少一个有效实例返回 true，否则返回 false
 */
bool UGameBuffComponent::HasBuff(FGameplayTag InBuffTag) const {
    return FindInstanceIndex(InBuffTag) != INDEX_NONE;
}

/**
 * @brief 获取指定 Buff 标签的当前聚合总层数
 * @param InBuffTag 目标 Buff 身份标签
 * @return 聚合层数数值；未激活返回 0
 */
int32 UGameBuffComponent::GetBuffStacks(FGameplayTag InBuffTag) const {
    if (!InBuffTag.IsValid()) {
        return 0;
    }
    int32 TotalStacks = 0;
    for (const FGameBuffInstance& Instance : ActiveBuffs.Items) {
        if (Instance.Spec.BuffTag.MatchesTagExact(InBuffTag)) {
            TotalStacks += Instance.Stacks;
        }
    }
    return TotalStacks;
}

/**
 * @brief 获取指定 Buff 标签的最大剩余持续时间（秒）
 * @param InBuffTag 目标 Buff 身份标签
 * @return 剩余秒数；永久生效或未激活返回 0.0f
 */
float UGameBuffComponent::GetBuffRemainingTime(FGameplayTag InBuffTag) const {
    if (!InBuffTag.IsValid()) {
        return 0.0f;
    }
    float Remaining = 0.0f;
    for (const FGameBuffInstance& Instance : ActiveBuffs.Items) {
        if (Instance.Spec.BuffTag.MatchesTagExact(InBuffTag)) {
            Remaining = FMath::Max(Remaining, Instance.RemainingSeconds);
        }
    }
    return Remaining;
}

/**
 * @brief 获取当前全部激活的 Buff 身份标签列表（自动去重）
 * @param OutTags 输出的标签数组
 */
void UGameBuffComponent::GetActiveBuffTags(TArray<FGameplayTag>& OutTags) const {
    OutTags.Reset();
    OutTags.Reserve(ActiveBuffs.Items.Num());
    for (const FGameBuffInstance& Instance : ActiveBuffs.Items) {
        OutTags.AddUnique(Instance.Spec.BuffTag);
    }
}

/**
 * @brief 获取当前全部激活 Buff 赋予的聚合状态标签集合
 * @param OutTags 输出的状态标签容器
 */
void UGameBuffComponent::GetGrantedTags(FGameplayTagContainer& OutTags) const {
    OutTags.Reset();
    for (const FGameBuffInstance& Instance : ActiveBuffs.Items) {
        OutTags.AppendTags(Instance.Spec.GrantedTags);
    }
}

/**
 * @brief 根据宿主网络权限判断当前端是否允许施加或移除 Buff
 * @return 允许操作返回 true，否则返回 false
 */
bool UGameBuffComponent::IsApplyAllowed() const {
    const AActor* OwnerActor = GetOwner();
    return OwnerActor == nullptr || OwnerActor->HasAuthority() || !bRequireAuthority;
}

/**
 * @brief 返回绑定的数值组件，未绑定时从宿主查找并缓存
 * @return 关联的数值组件指针；未找到时返回 nullptr
 */
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

/**
 * @brief 将指定实例的修正器挂接到数值组件
 * @param InInstance 待挂接的 Buff 实例
 */
void UGameBuffComponent::RegisterInstanceModifiers(const FGameBuffInstance& InInstance) {
    if (UGameStatComponent* StatComponent = ResolveStatComponent()) {
        ModifierBridge.Register(InInstance.InstanceId, InInstance, *StatComponent);
    }
}

/**
 * @brief 从数值组件注销指定实例的全部修正器
 * @param InInstance 待注销的 Buff 实例
 */
void UGameBuffComponent::UnregisterInstanceModifiers(const FGameBuffInstance& InInstance) {
    ModifierBridge.Unregister(InInstance.InstanceId, ResolveStatComponent());
}

/**
 * @brief 查找首个匹配指定 Buff 标签的实例下标
 * @param InBuffTag 目标 Buff 身份标签
 * @return 数组下标；未找到返回 INDEX_NONE
 */
int32 UGameBuffComponent::FindInstanceIndex(const FGameplayTag& InBuffTag) const {
    if (!InBuffTag.IsValid()) {
        return INDEX_NONE;
    }
    for (int32 Index = 0; Index < ActiveBuffs.Items.Num(); ++Index) {
        if (ActiveBuffs.Items[Index].Spec.BuffTag.MatchesTagExact(InBuffTag)) {
            return Index;
        }
    }
    return INDEX_NONE;
}

/**
 * @brief 依据当前是否有带倒计时的实例动态启闭组件 Tick
 */
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

/**
 * @brief 客户端在 FastArray 复制后计算层数差分并精准触发变更或移除委托
 * @param InItems 最新复制的实例列表
 * @param ExcludedIndices 暂未完成删除的排除条目下标视图
 * @param ForcedChangedTags 强制要求广播变更的标签视图
 */
void UGameBuffComponent::PublishReplicatedDiff(
    const TArray<FGameBuffInstance>& InItems,
    TArrayView<const int32> ExcludedIndices,
    TArrayView<const FGameplayTag> ForcedChangedTags
) {
    TMap<FGameplayTag, int32> CurrentStacks;
    CurrentStacks.Reserve(InItems.Num());
    for (int32 Index = 0; Index < InItems.Num(); ++Index) {
        if (ExcludedIndices.Contains(Index)) {
            continue;
        }

        const FGameBuffInstance& Instance = InItems[Index];
        CurrentStacks.FindOrAdd(Instance.Spec.BuffTag) += Instance.Stacks;
    }

    TArray<FGameplayTag> RemovedTags;
    RemovedTags.Reserve(AcknowledgedStacks.Num());
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
        if (PreviousStacks == nullptr || *PreviousStacks != Pair.Value || ForcedChangedTags.Contains(Pair.Key)) {
            BroadcastBuffChanged(Pair.Key, Pair.Value);
        }
    }
}

/**
 * @brief 在条目被移除后判断该标签是否仍有剩余实例并广播相应状态
 * @param InBuffTag 目标 Buff 身份标签
 */
void UGameBuffComponent::BroadcastTagStateAfterRemoval(const FGameplayTag& InBuffTag) {
    if (FindInstanceIndex(InBuffTag) != INDEX_NONE) {
        BroadcastBuffChanged(InBuffTag, GetBuffStacks(InBuffTag));
    } else {
        BroadcastBuffRemoved(InBuffTag);
    }
}

/**
 * @brief 更新已确认层数并向动态与原生委托广播 Buff 变更
 * @param InBuffTag 目标 Buff 身份标签
 * @param InStacks 最新层数
 */
void UGameBuffComponent::BroadcastBuffChanged(const FGameplayTag& InBuffTag, int32 InStacks) {
    AcknowledgedStacks.FindOrAdd(InBuffTag) = InStacks;
    OnBuffChanged.Broadcast(InBuffTag, InStacks);
    OnBuffChangedNative.Broadcast(InBuffTag, InStacks);
}

/**
 * @brief 从已确认表中移除并向动态与原生委托广播 Buff 完全移除
 * @param InBuffTag 目标 Buff 身份标签
 */
void UGameBuffComponent::BroadcastBuffRemoved(const FGameplayTag& InBuffTag) {
    AcknowledgedStacks.Remove(InBuffTag);
    OnBuffRemoved.Broadcast(InBuffTag);
    OnBuffRemovedNative.Broadcast(InBuffTag);
}
