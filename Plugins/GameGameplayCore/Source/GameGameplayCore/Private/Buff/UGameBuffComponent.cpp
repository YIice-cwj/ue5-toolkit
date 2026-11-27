/**
 * @file UGameBuffComponent.cpp
 * @brief 通用 Buff / 状态组件实现
 */

#include "Buff/UGameBuffComponent.h"

#include "GameFramework/Actor.h"
#include "GameGameplayCoreLog.h"
#include "Net/UnrealNetwork.h"
#include "Stat/UGameStatComponent.h"

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
void UGameBuffComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction
) {
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (ActiveBuffs.Items.IsEmpty()) {
        SetComponentTickEnabled(false);
        return;
    }

    if (!IsAuthoritativeWriteAllowed()) {
        TickSimulatedCountdown(DeltaTime);
        return;
    }

    TickAuthoritativeExpiration(DeltaTime);
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
    ClearBoundModifiers();
    Super::EndPlay(EndPlayReason);
}

/**
 * @brief 从对象池出库激活回调
 */
void UGameBuffComponent::OnAcquireFromPool_Implementation() {
    RefreshTickState();
}

/**
 * @brief 归还对象池休眠回调：静默清空所有 Buff 实例并静默回滚修正器（保持实例 ID 单调递增防联机碰撞，零业务广播）
 */
void UGameBuffComponent::OnReleaseToPool_Implementation() {
    if (!ActiveBuffs.Items.IsEmpty()) {
        ActiveBuffs.Items.Reset();
        ActiveBuffs.MarkArrayDirty();
    }
    SetComponentTickEnabled(false);
    AcknowledgedStacks.Reset();
    ClearBoundModifiersSilent();

    GAME_GAMEPLAY_LOG(
        LogGameBuff,
        Verbose,
        TEXT("宿主 [%s] 归还对象池，已静默清空全部 Buff 状态。"),
        *GetNameSafe(GetOwner())
    );
}

/**
 * @brief 显式绑定数值组件，回滚旧组件修正器并将全部活跃 Buff 迁移至新组件（传入 nullptr 时显式解绑且不再自动回绑宿主数值组件）
 * @param InStatComponent 目标数值组件指针；为空时显式解绑并回滚
 */
void UGameBuffComponent::BindStatComponent(UGameStatComponent* InStatComponent) {
    const bool bNewExplicitlyUnbound = (InStatComponent == nullptr);
    if (BoundStatComponent.Get() == InStatComponent && bExplicitlyUnbound == bNewExplicitlyUnbound) {
        return;
    }

    UGameStatComponent* OldStatComponent = BoundStatComponent.Get();
    BoundStatComponent = InStatComponent;
    bExplicitlyUnbound = bNewExplicitlyUnbound;

    if (OldStatComponent != nullptr) {
        ModifierBridge.ClearAll(*OldStatComponent);
    } else {
        ModifierBridge.Reset();
    }

    if (InStatComponent == nullptr || BoundStatComponent.Get() != InStatComponent) {
        return;
    }

    UGameStatComponent::FScopedModifierBatch Batch(*InStatComponent);
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
 * @param InSpec Buff 完整描述配置（Duration 须为有限浮点）
 * @return 成功返回实例 ID；参数非法返回 InvalidParameter，非权威返回 NotAuthority，达叠层上限返回 MaxBuffStacksReached
 */
TGameStatusResult<int32> UGameBuffComponent::ApplyBuff(const FGameBuffSpec& InSpec) {
    if (!InSpec.BuffTag.IsValid() || !FMath::IsFinite(InSpec.Duration)) {
        GAME_GAMEPLAY_LOG(
            LogGameBuff,
            Warning,
            TEXT("拒绝施加 Buff：身份标签无效或持续时间为非有限浮点 (Tag=%s)。"),
            *InSpec.BuffTag.ToString()
        );
        return TGameStatusResult<int32>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::InvalidParameter,
            TEXT("Invalid BuffTag or non-finite duration")
        );
    }

    if (!IsAuthoritativeWriteAllowed()) {
        GAME_GAMEPLAY_LOG_THROTTLE(
            LogGameBuff,
            1.0,
            Warning,
            TEXT("非权威端拒绝施加 Buff: %s"),
            *InSpec.BuffTag.ToString()
        );
        return TGameStatusResult<int32>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::NotAuthority,
            TEXT("Not authority")
        );
    }

    if (InSpec.StackPolicy == EGameBuffStackPolicy::Independent
        && InSpec.MaxStacks > 0
        && GetBuffStacks(InSpec.BuffTag) >= InSpec.MaxStacks) {
        GAME_GAMEPLAY_LOG(
            LogGameBuff,
            Verbose,
            TEXT("宿主 [%s] 施加 Buff [%s] 被拦截：已达最大层数 %d。"),
            *GetNameSafe(GetOwner()),
            *InSpec.BuffTag.ToString(),
            InSpec.MaxStacks
        );
        return TGameStatusResult<int32>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::MaxBuffStacksReached,
            TEXT("Max buff stacks reached")
        );
    }

    const int32 ExistingIndex = InSpec.StackPolicy != EGameBuffStackPolicy::Independent
        ? FindInstanceIndex(InSpec.BuffTag)
        : INDEX_NONE;

    const int32 InstanceId = (ExistingIndex != INDEX_NONE)
        ? ApplyToExistingInstance(ActiveBuffs.Items[ExistingIndex], InSpec)
        : CreateNewInstance(InSpec);
    return TGameStatusResult<int32>::Success(InstanceId);
}

/**
 * @brief 按 Buff 身份标签移除实例（蓝图轻量门面）
 * @param InBuffTag 目标 Buff 身份标签
 * @param bRemoveAllInstances 是否移除该标签下的全部实例（若为 false 则仅移除一个实例）
 * @return 成功移除至少一个实例返回 true，未找到匹配实例返回 false
 */
bool UGameBuffComponent::K2_RemoveBuff(const FGameplayTag& InBuffTag, bool bRemoveAllInstances) {
    return RemoveBuff(InBuffTag, bRemoveAllInstances).IsSuccess();
}

/**
 * @brief 按 Buff 身份标签移除实例（强类型契约）
 * @param InBuffTag 目标 Buff 身份标签
 * @param bRemoveAllInstances 是否移除该标签下的全部实例（若为 false 则仅移除一个实例）
 * @return 成功移除返回 Success；参数非法返回 InvalidParameter，非权威返回 NotAuthority，未找到返回 BuffNotFound
 */
TGameStatusResult<void> UGameBuffComponent::RemoveBuff(const FGameplayTag& InBuffTag, bool bRemoveAllInstances) {
    if (!InBuffTag.IsValid()) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::InvalidParameter,
            TEXT("Invalid BuffTag")
        );
    }

    if (!IsAuthoritativeWriteAllowed()) {
        GAME_GAMEPLAY_LOG_THROTTLE(
            LogGameBuff,
            1.0,
            Warning,
            TEXT("非权威端拒绝移除 Buff: %s"),
            *InBuffTag.ToString()
        );
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::NotAuthority,
            TEXT("Not authority")
        );
    }

    bool bRemoved = false;
    UGameStatComponent* StatComp = ResolveStatComponent();
    TOptional<UGameStatComponent::FScopedModifierBatch> Batch;
    if (StatComp != nullptr) {
        Batch.Emplace(*StatComp);
    }

    for (int32 Index = ActiveBuffs.Items.Num() - 1; Index >= 0; --Index) {
        if (!ActiveBuffs.Items[Index].Spec.BuffTag.MatchesTagExact(InBuffTag)) {
            continue;
        }

        const int32 InstanceId = ActiveBuffs.Items[Index].InstanceId;
        ActiveBuffs.Items.RemoveAt(Index);
        ModifierBridge.Unregister(InstanceId, StatComp);
        bRemoved = true;
        if (!bRemoveAllInstances) {
            break;
        }
    }

    if (!bRemoved) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::BuffNotFound,
            TEXT("Buff not found")
        );
    }

    ActiveBuffs.MarkArrayDirty();
    RefreshTickState();
    Batch.Reset();
    BroadcastTagStateAfterRemoval(InBuffTag);
    GAME_GAMEPLAY_LOG(
        LogGameBuff,
        Verbose,
        TEXT("宿主 [%s] 移除 Buff [%s] 成功 (全实例移除=%s)。"),
        *GetNameSafe(GetOwner()),
        *InBuffTag.ToString(),
        bRemoveAllInstances ? TEXT("True") : TEXT("False")
    );
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 按 Buff 身份标签扣减指定层数（蓝图轻量门面）
 * @param InBuffTag 目标 Buff 身份标签
 * @param StacksToRemove 扣减层数（默认 1 层）
 * @return 成功扣减返回 true，实例不存在或扣减失败返回 false
 */
bool UGameBuffComponent::K2_RemoveBuffStacks(const FGameplayTag& InBuffTag, int32 StacksToRemove) {
    return RemoveBuffStacks(InBuffTag, StacksToRemove).IsSuccess();
}

/**
 * @brief 按 Buff 身份标签扣减指定层数（强类型契约）
 * @param InBuffTag 目标 Buff 身份标签
 * @param StacksToRemove 扣减层数（默认 1 层）
 * @return 成功扣减返回 Success；参数非法返回 InvalidParameter，非权威返回 NotAuthority，未找到返回 BuffNotFound
 */
TGameStatusResult<void> UGameBuffComponent::RemoveBuffStacks(const FGameplayTag& InBuffTag, int32 StacksToRemove) {
    if (!InBuffTag.IsValid() || StacksToRemove <= 0) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::InvalidParameter,
            TEXT("Invalid BuffTag or StacksToRemove")
        );
    }

    if (!IsAuthoritativeWriteAllowed()) {
        GAME_GAMEPLAY_LOG_THROTTLE(
            LogGameBuff,
            1.0,
            Warning,
            TEXT("非权威端拒绝扣减 Buff 层数: %s"),
            *InBuffTag.ToString()
        );
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::NotAuthority,
            TEXT("Not authority")
        );
    }

    int32 RemainingToRemove = StacksToRemove;
    bool bChanged = false;
    bool bRemovedAnyInstance = false;

    UGameStatComponent* StatComp = ResolveStatComponent();
    TOptional<UGameStatComponent::FScopedModifierBatch> Batch;
    if (StatComp != nullptr) {
        Batch.Emplace(*StatComp);
    }

    for (int32 Index = ActiveBuffs.Items.Num() - 1; Index >= 0 && RemainingToRemove > 0; --Index) {
        FGameBuffInstance& Instance = ActiveBuffs.Items[Index];
        if (!Instance.Spec.BuffTag.MatchesTagExact(InBuffTag)) {
            continue;
        }

        bChanged = true;
        if (Instance.Stacks <= RemainingToRemove) {
            RemainingToRemove -= Instance.Stacks;
            const int32 InstanceId = Instance.InstanceId;
            ActiveBuffs.Items.RemoveAt(Index);
            ModifierBridge.Unregister(InstanceId, StatComp);
            bRemovedAnyInstance = true;
        } else {
            Instance.Stacks -= RemainingToRemove;
            RemainingToRemove = 0;
            ActiveBuffs.MarkItemDirty(Instance);
            if (StatComp != nullptr) {
                ModifierBridge.Register(Instance.InstanceId, Instance, *StatComp);
            }
        }
    }

    if (!bChanged) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::BuffNotFound,
            TEXT("Buff not found")
        );
    }

    if (bRemovedAnyInstance) {
        ActiveBuffs.MarkArrayDirty();
        RefreshTickState();
    }
    Batch.Reset();
    BroadcastTagStateAfterRemoval(InBuffTag);
    GAME_GAMEPLAY_LOG(
        LogGameBuff,
        Verbose,
        TEXT("宿主 [%s] 扣减 Buff [%s] 层数成功 (请求扣减: %d)。"),
        *GetNameSafe(GetOwner()),
        *InBuffTag.ToString(),
        StacksToRemove
    );
    return TGameStatusResult<void>::Success();
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
 * @return 成功移除返回 Success；参数非法返回 InvalidParameter，非权威返回 NotAuthority，未找到返回 BuffNotFound
 */
TGameStatusResult<void> UGameBuffComponent::RemoveBuffInstance(int32 InInstanceId) {
    if (InInstanceId <= 0) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::InvalidParameter,
            TEXT("Invalid InstanceId")
        );
    }

    if (!IsAuthoritativeWriteAllowed()) {
        GAME_GAMEPLAY_LOG_THROTTLE(
            LogGameBuff,
            1.0,
            Warning,
            TEXT("非权威端拒绝移除 Buff 实例: %d"),
            InInstanceId
        );
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::NotAuthority,
            TEXT("Not authority")
        );
    }

    for (int32 Index = 0; Index < ActiveBuffs.Items.Num(); ++Index) {
        if (ActiveBuffs.Items[Index].InstanceId != InInstanceId) {
            continue;
        }

        const FGameplayTag RemovedTag = ActiveBuffs.Items[Index].Spec.BuffTag;
        ActiveBuffs.Items.RemoveAt(Index);
        ActiveBuffs.MarkArrayDirty();
        RefreshTickState();
        ModifierBridge.Unregister(InInstanceId, ResolveStatComponent());
        BroadcastTagStateAfterRemoval(RemovedTag);
        GAME_GAMEPLAY_LOG(
            LogGameBuff,
            Verbose,
            TEXT("宿主 [%s] 移除 Buff 实例成功: ID=%d, 标签=[%s]。"),
            *GetNameSafe(GetOwner()),
            InInstanceId,
            *RemovedTag.ToString()
        );
        return TGameStatusResult<void>::Success();
    }

    return TGameStatusResult<void>::Failure(
        Game::ModuleId::Gameplay,
        Game::GameplayCoreErrorCodes::BuffNotFound,
        TEXT("Buff instance not found")
    );
}

/**
 * @brief 批量清除当前全部活跃 Buff 并逐标签广播移除事件（蓝图轻量门面）
 * @return 被清除的 Buff 实例总数；非权威被拒时返回 0
 */
int32 UGameBuffComponent::K2_ClearAllBuffs() {
    const TGameStatusResult<int32> Result = ClearAllBuffs();
    return Result.IsSuccess() ? Result.GetValue() : 0;
}

/**
 * @brief 批量清除当前全部活跃 Buff 并逐标签广播移除事件（强类型契约）
 * @return 成功返回被清除的 Buff 实例总数；非权威端返回 NotAuthority
 */
TGameStatusResult<int32> UGameBuffComponent::ClearAllBuffs() {
    if (!IsAuthoritativeWriteAllowed()) {
        GAME_GAMEPLAY_LOG_THROTTLE(LogGameBuff, 1.0, Warning, TEXT("非权威端拒绝批量清空 Buff。"));
        return TGameStatusResult<int32>::Failure(
            Game::ModuleId::Gameplay,
            Game::GameplayCoreErrorCodes::NotAuthority,
            TEXT("Not authority")
        );
    }

    const int32 RemovedCount = ActiveBuffs.Items.Num();
    if (RemovedCount == 0) {
        return TGameStatusResult<int32>::Success(0);
    }

    TArray<FGameplayTag, TInlineAllocator<8>> RemovedTags;
    RemovedTags.Reserve(RemovedCount);
    for (const FGameBuffInstance& Instance : ActiveBuffs.Items) {
        RemovedTags.AddUnique(Instance.Spec.BuffTag);
    }

    ActiveBuffs.Items.Reset();
    ActiveBuffs.MarkArrayDirty();
    SetComponentTickEnabled(false);
    AcknowledgedStacks.Reset();

    ClearBoundModifiers();

    for (const FGameplayTag& Tag : RemovedTags) {
        OnBuffRemoved.Broadcast(Tag);
        OnBuffRemovedNative.Broadcast(Tag);
    }

    GAME_GAMEPLAY_LOG(
        LogGameBuff,
        Verbose,
        TEXT("宿主 [%s] 批量清空全部 Buff，共移除 %d 个实例。"),
        *GetNameSafe(GetOwner()),
        RemovedCount
    );
    return TGameStatusResult<int32>::Success(RemovedCount);
}

/**
 * @brief 查询当前是否存在指定标签的 Buff 实例
 * @param InBuffTag 目标 Buff 标签
 * @return 存在至少一个活跃实例返回 true，否则返回 false
 */
bool UGameBuffComponent::HasBuff(const FGameplayTag& InBuffTag) const {
    return FindInstanceIndex(InBuffTag) != INDEX_NONE;
}

/**
 * @brief 零拷贝快速查询当前全部活跃 Buff 是否授予了指定状态标签（支持父标签层级匹配）
 * @param InTag 目标状态标签（如 Status.Stun）
 * @return 任一活跃 Buff 的 GrantedTags 包含该标签返回 true
 */
bool UGameBuffComponent::HasGrantedTag(const FGameplayTag& InTag) const {
    if (!InTag.IsValid()) {
        return false;
    }
    for (const FGameBuffInstance& Instance : ActiveBuffs.Items) {
        if (Instance.Spec.GrantedTags.HasTag(InTag)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 聚合指定标签所有活跃实例的层数总和
 * @param InBuffTag 目标 Buff 标签
 * @return 聚合后的总层数；无实例时返回 0
 */
int32 UGameBuffComponent::GetBuffStacks(const FGameplayTag& InBuffTag) const {
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
 * @brief 获取指定标签所有实例中的最大剩余时长
 * @param InBuffTag 目标 Buff 标签
 * @return 最大剩余时间（秒）；永久 Buff 或无实例时返回 0.0f
 */
float UGameBuffComponent::GetBuffRemainingTime(const FGameplayTag& InBuffTag) const {
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
 * @brief 输出当前所有激活中的 Buff 身份标签（去重列表）
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
 * @brief 聚合当前全部 Buff 所赋予的状态标签集合（如眩晕、减速）
 * @param OutTags 聚合输出的标签容器
 */
void UGameBuffComponent::GetGrantedTags(FGameplayTagContainer& OutTags) const {
    OutTags.Reset();
    for (const FGameBuffInstance& Instance : ActiveBuffs.Items) {
        OutTags.AppendTags(Instance.Spec.GrantedTags);
    }
}

/**
 * @brief 查询当前是否允许写入（施加、移除或清空）Buff（根据宿主网络权限与组件配置判定）
 * @return 允许操作返回 true，非权威且受限时返回 false
 */
bool UGameBuffComponent::IsAuthoritativeWriteAllowed() const {
    const AActor* OwnerActor = GetOwner();
    return OwnerActor == nullptr || OwnerActor->HasAuthority() || bAllowNonAuthoritativeWrites;
}

/**
 * @brief 非权威端执行本地剩余时间倒计时并在全部计时结束后关闭 Tick
 * @param DeltaTime 帧时间间隔（秒）
 */
void UGameBuffComponent::TickSimulatedCountdown(float DeltaTime) {
    bool bHasActiveTimer = false;
    for (FGameBuffInstance& Instance : ActiveBuffs.Items) {
        if (Instance.Spec.Duration <= 0.0f) {
            continue;
        }
        Instance.RemainingSeconds = FMath::Max(0.0f, Instance.RemainingSeconds - DeltaTime);
        bHasActiveTimer = bHasActiveTimer || (Instance.RemainingSeconds > 0.0f);
    }

    if (!bHasActiveTimer) {
        SetComponentTickEnabled(false);
    }
}

/**
 * @brief 权威端推进到期计时、批量移除过期实例并广播变更或移除
 * @param DeltaTime 帧时间间隔（秒）
 */
void UGameBuffComponent::TickAuthoritativeExpiration(float DeltaTime) {
    TArray<FGameplayTag, TInlineAllocator<8>> ExpiredTags;
    UGameStatComponent* StatComp = ResolveStatComponent();
    TOptional<UGameStatComponent::FScopedModifierBatch> Batch;
    if (StatComp != nullptr) {
        Batch.Emplace(*StatComp);
    }

    for (int32 Index = ActiveBuffs.Items.Num() - 1; Index >= 0; --Index) {
        FGameBuffInstance& Instance = ActiveBuffs.Items[Index];
        if (Instance.Spec.Duration <= 0.0f) {
            continue;
        }

        Instance.RemainingSeconds -= DeltaTime;
        if (Instance.RemainingSeconds > 0.0f) {
            continue;
        }

        const int32 InstanceId = Instance.InstanceId;
        const FGameplayTag BuffTag = Instance.Spec.BuffTag;
        GAME_GAMEPLAY_LOG(
            LogGameBuff,
            Verbose,
            TEXT("宿主 [%s] 的 Buff [%s] (实例 ID: %d) 自然过期注销。"),
            *GetNameSafe(GetOwner()),
            *BuffTag.ToString(),
            InstanceId
        );
        ExpiredTags.AddUnique(BuffTag);
        ActiveBuffs.Items.RemoveAt(Index);
        ModifierBridge.Unregister(InstanceId, StatComp);
    }

    if (ExpiredTags.IsEmpty()) {
        return;
    }

    ActiveBuffs.MarkArrayDirty();
    RefreshTickState();
    Batch.Reset();
    for (const FGameplayTag& ExpiredTag : ExpiredTags) {
        BroadcastTagStateAfterRemoval(ExpiredTag);
    }
}

/**
 * @brief 在已有同标签 Buff 实例上叠加层数、刷新或累加时长并原子同步修正器
 * @param Existing 待更新的既有实例引用
 * @param InSpec 新施加的 Buff 规格配置
 * @return 更新后的实例 ID
 */
int32 UGameBuffComponent::ApplyToExistingInstance(FGameBuffInstance& Existing, const FGameBuffSpec& InSpec) {
    if (InSpec.MaxStacks <= 0 || Existing.Stacks < InSpec.MaxStacks) {
        ++Existing.Stacks;
    }

    if (InSpec.StackPolicy == EGameBuffStackPolicy::AddDuration && InSpec.Duration > 0.0f) {
        Existing.RemainingSeconds += InSpec.Duration;
    } else {
        Existing.RemainingSeconds = InSpec.Duration > 0.0f ? InSpec.Duration : 0.0f;
    }

    Existing.Spec = InSpec;
    ActiveBuffs.MarkItemDirty(Existing);
    const int32 InstanceId = Existing.InstanceId;
    const FGameplayTag BuffTag = Existing.Spec.BuffTag;
    const float RemainingSeconds = Existing.RemainingSeconds;
    RefreshTickState();

    RegisterInstanceModifiers(Existing);

    const int32 TotalStacks = GetBuffStacks(BuffTag);
    if (TotalStacks > 0) {
        BroadcastBuffChanged(BuffTag, TotalStacks);
    }
    GAME_GAMEPLAY_LOG(
        LogGameBuff,
        Verbose,
        TEXT("宿主 [%s] 叠加 Buff [%s]: 实例 ID=%d, 聚合层数=%d, 剩余时长=%.2f 秒。"),
        *GetNameSafe(GetOwner()),
        *BuffTag.ToString(),
        InstanceId,
        TotalStacks,
        RemainingSeconds
    );
    return InstanceId;
}

/**
 * @brief 创建全新的 Buff 运行实例、挂接修正器并广播变更
 * @param InSpec 新施加的 Buff 规格配置
 * @return 新分配的实例 ID
 */
int32 UGameBuffComponent::CreateNewInstance(const FGameBuffSpec& InSpec) {
    const int32 AllocatedId = NextInstanceId;
    NextInstanceId = (NextInstanceId < MAX_int32) ? (NextInstanceId + 1) : 1;

    FGameBuffInstance& Added = ActiveBuffs.Items.AddDefaulted_GetRef();
    Added.InstanceId = AllocatedId;
    Added.Spec = InSpec;
    Added.Stacks = 1;
    Added.RemainingSeconds = InSpec.Duration > 0.0f ? InSpec.Duration : 0.0f;

    ActiveBuffs.MarkItemDirty(Added);
    const FGameplayTag BuffTag = Added.Spec.BuffTag;
    const float RemainingSeconds = Added.RemainingSeconds;
    RefreshTickState();

    RegisterInstanceModifiers(Added);

    const int32 TotalStacks = GetBuffStacks(BuffTag);
    if (TotalStacks > 0) {
        BroadcastBuffChanged(BuffTag, TotalStacks);
    }
    GAME_GAMEPLAY_LOG(
        LogGameBuff,
        Verbose,
        TEXT("宿主 [%s] 施加 Buff [%s]: 实例 ID=%d, 聚合层数=%d, 持续时间=%.2f 秒。"),
        *GetNameSafe(GetOwner()),
        *BuffTag.ToString(),
        AllocatedId,
        TotalStacks,
        RemainingSeconds
    );
    return AllocatedId;
}

/**
 * @brief 回滚已绑定数值组件上的全部 Buff 修正器，或在组件已失效时清空句柄映射
 */
void UGameBuffComponent::ClearBoundModifiers() {
    if (UGameStatComponent* StatComponent = BoundStatComponent.Get()) {
        ModifierBridge.ClearAll(*StatComponent);
    } else {
        ModifierBridge.Reset();
    }
}

/**
 * @brief 静默回滚已绑定数值组件上的全部 Buff 修正器（零业务广播，专供对象池归还使用）
 */
void UGameBuffComponent::ClearBoundModifiersSilent() {
    if (UGameStatComponent* StatComponent = BoundStatComponent.Get()) {
        ModifierBridge.ClearAllSilent(*StatComponent);
    } else {
        ModifierBridge.Reset();
    }
}

/**
 * @brief 返回显式绑定的数值组件；未显式解绑且未绑定时从宿主自动发现并缓存
 * @return 可用的数值组件；显式解绑、无宿主或未发现时返回 nullptr
 */
UGameStatComponent* UGameBuffComponent::ResolveStatComponent() {
    if (BoundStatComponent.IsValid()) {
        return BoundStatComponent.Get();
    }

    if (bExplicitlyUnbound) {
        return nullptr;
    }

    if (const AActor* OwnerActor = GetOwner()) {
        UGameStatComponent* FoundComponent = OwnerActor->FindComponentByClass<UGameStatComponent>();
        BoundStatComponent = FoundComponent;
        return FoundComponent;
    }
    return nullptr;
}

/**
 * @brief 将单个 Buff 实例按当前层数原子挂接或原地更新到已解析的数值组件
 * @param InInstance 目标 Buff 运行实例
 */
void UGameBuffComponent::RegisterInstanceModifiers(const FGameBuffInstance& InInstance) {
    if (UGameStatComponent* StatComponent = ResolveStatComponent()) {
        ModifierBridge.Register(InInstance.InstanceId, InInstance, *StatComponent);
    }
}

/**
 * @brief 按实例 ID 回滚桥接器记录的属性修正器
 * @param InInstance 待回滚的 Buff 运行实例
 */
void UGameBuffComponent::UnregisterInstanceModifiers(const FGameBuffInstance& InInstance) {
    ModifierBridge.Unregister(InInstance.InstanceId, ResolveStatComponent());
}

/**
 * @brief 查找指定标签的首个活跃 Buff 实例
 * @param InBuffTag 目标 Buff 身份标签
 * @return 首个匹配实例下标；不存在时返回 INDEX_NONE
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
 * @brief 根据排除指定下标后是否仍存在未到期的限时实例启停组件 Tick
 * @param ExcludedIndices 需要排除的待删除条目下标视图（用于 PreReplicatedRemove）
 */
void UGameBuffComponent::RefreshTickState(TConstArrayView<int32> ExcludedIndices) {
    bool bHasTimedInstance = false;
    for (int32 Index = 0; Index < ActiveBuffs.Items.Num(); ++Index) {
        if (ExcludedIndices.Contains(Index)) {
            continue;
        }
        const FGameBuffInstance& Instance = ActiveBuffs.Items[Index];
        if (Instance.Spec.Duration > 0.0f && Instance.RemainingSeconds > 0.0f) {
            bHasTimedInstance = true;
            break;
        }
    }
    SetComponentTickEnabled(bHasTimedInstance);
}

/**
 * @brief 对比已确认层数与复制后的条目状态，广播移除及新增、叠层或指定强制变化
 * @param InItems 复制回调当前持有的 Buff 条目
 * @param ExcludedIndices 复制删除前需要从当前状态排除的条目下标
 * @param ForcedChangedTags 即使层数不变也必须广播变更的 Buff 标签
 */
void UGameBuffComponent::PublishReplicatedDiff(
    const TArray<FGameBuffInstance>& InItems,
    TConstArrayView<int32> ExcludedIndices,
    TConstArrayView<FGameplayTag> ForcedChangedTags
) {
    TSortedVectorMap<FGameFastTagKey, int32, 8> CurrentStacks;
    CurrentStacks.Reserve(InItems.Num());
    for (int32 Index = 0; Index < InItems.Num(); ++Index) {
        if (ExcludedIndices.Contains(Index)) {
            continue;
        }

        const FGameBuffInstance& Instance = InItems[Index];
        if (!Instance.Spec.BuffTag.IsValid()) {
            continue;
        }
        CurrentStacks.FindOrAdd(FGameFastTagKey(Instance.Spec.BuffTag)) += Instance.Stacks;
    }

    TArray<FGameplayTag, TInlineAllocator<8>> RemovedTags;
    RemovedTags.Reserve(AcknowledgedStacks.Num());
    for (const auto& Entry : AcknowledgedStacks.GetEntries()) {
        if (!CurrentStacks.Contains(Entry.Key)) {
            RemovedTags.Add(Entry.Key.Tag);
        }
    }
    for (const FGameplayTag& RemovedTag : RemovedTags) {
        BroadcastBuffRemoved(RemovedTag);
    }

    for (const auto& Entry : CurrentStacks.GetEntries()) {
        const int32* PreviousStacks = AcknowledgedStacks.Find(Entry.Key);
        if (PreviousStacks == nullptr
            || *PreviousStacks != Entry.Value
            || ForcedChangedTags.Contains(Entry.Key.Tag)) {
            BroadcastBuffChanged(Entry.Key.Tag, Entry.Value);
        }
    }
}

/**
 * @brief 删除后按标签剩余实例广播层数变化或完全移除
 * @param InBuffTag 发生删除的 Buff 身份标签
 */
void UGameBuffComponent::BroadcastTagStateAfterRemoval(const FGameplayTag& InBuffTag) {
    if (FindInstanceIndex(InBuffTag) != INDEX_NONE) {
        BroadcastBuffChanged(InBuffTag, GetBuffStacks(InBuffTag));
    } else {
        BroadcastBuffRemoved(InBuffTag);
    }
}

/**
 * @brief 记录已确认层数并向蓝图和原生订阅者广播 Buff 变更
 * @param InBuffTag 发生变化的 Buff 身份标签
 * @param InStacks 变更后的聚合层数
 */
void UGameBuffComponent::BroadcastBuffChanged(const FGameplayTag& InBuffTag, int32 InStacks) {
    AcknowledgedStacks.FindOrAdd(FGameFastTagKey(InBuffTag)) = InStacks;
    OnBuffChanged.Broadcast(InBuffTag, InStacks);
    OnBuffChangedNative.Broadcast(InBuffTag, InStacks);
}

/**
 * @brief 清除已确认层数并向蓝图和原生订阅者广播 Buff 完全移除
 * @param InBuffTag 已完全移除的 Buff 身份标签
 */
void UGameBuffComponent::BroadcastBuffRemoved(const FGameplayTag& InBuffTag) {
    AcknowledgedStacks.Remove(FGameFastTagKey(InBuffTag));
    OnBuffRemoved.Broadcast(InBuffTag);
    OnBuffRemovedNative.Broadcast(InBuffTag);
}
