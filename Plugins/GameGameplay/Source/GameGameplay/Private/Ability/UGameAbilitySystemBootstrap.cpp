/**
 * @file UGameAbilitySystemBootstrap.cpp
 * @brief GAS 引导通用组件实现（触发矩阵全时序固化）
 */

#include "Ability/UGameAbilitySystemBootstrap.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GameGameplayLog.h"
#include "TimerManager.h"

namespace {

constexpr float MinRetryIntervalSeconds = 0.01f;

}  // namespace

/**
 * @brief 构造函数：关闭组件 Tick 并用默认配置初始化重试闸门
 */
UGameAbilitySystemBootstrap::UGameAbilitySystemBootstrap() {
    PrimaryComponentTick.bCanEverTick = false;
    ConfigureRetryGates();
}

/**
 * @brief 组件启动生命周期回调：同步重试闸门配置并尝试兜底引导
 */
void UGameAbilitySystemBootstrap::BeginPlay() {
    Super::BeginPlay();
    ConfigureRetryGates();
    TryInitializeFromOwner();
}

/**
 * @brief 组件销毁生命周期回调：清理重试定时器并撤销本组件授予的全部默认能力
 * @param EndPlayReason 结束运行的原因
 */
void UGameAbilitySystemBootstrap::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    OnReleaseToPool_Implementation();
    if (HasBegunPlay()) {
        Super::EndPlay(EndPlayReason);
    }
}

/**
 * @brief 从对象池激活时重置重试门累计次数并同步最新重试配置
 */
void UGameAbilitySystemBootstrap::OnAcquireFromPool_Implementation() {
    InitRetryGate.Reset();
    ReplicationRetryGate.Reset();
    ConfigureRetryGates();
}

/**
 * @brief 归还对象池时清理定时器、撤销已授予能力并重置握手状态
 */
void UGameAbilitySystemBootstrap::OnReleaseToPool_Implementation() {
    if (UWorld* World = GetWorld()) {
        World->GetTimerManager().ClearTimer(InitRetryTimerHandle);
        World->GetTimerManager().ClearTimer(ReplicationRetryTimerHandle);
    }
    ClearGrantedAbilities();
    CurrentAvatar.Reset();
    CurrentASC.Reset();
    bInitialAttributesWritten = false;
    bDefaultAbilitiesGranted = false;
    InitRetryGate.Reset();
    ReplicationRetryGate.Reset();
}

/**
 * @brief 注入取 ASC 委托
 * @param InDelegate 取 ASC 委托
 */
void UGameAbilitySystemBootstrap::SetResolveAbilitySystemComponentDelegate(const FResolveAbilitySystemComponent& InDelegate) {
    ResolveAbilitySystemComponent = InDelegate;
}

/**
 * @brief 注入写初值委托
 * @param InDelegate 写初值委托
 */
void UGameAbilitySystemBootstrap::SetWriteInitialAttributesDelegate(const FWriteInitialAttributes& InDelegate) {
    WriteInitialAttributes = InDelegate;
}

/**
 * @brief 注入数据就绪判定委托
 * @param InDelegate 数据就绪判定委托
 */
void UGameAbilitySystemBootstrap::SetDataReadyQueryDelegate(const FQueryDataReady& InDelegate) {
    DataReadyQuery = InDelegate;
}

/**
 * @brief 配置引导完成后权威授予的默认能力类列表
 * @param InDefaultAbilities 默认能力类数组
 */
void UGameAbilitySystemBootstrap::SetDefaultAbilities(TArray<TSubclassOf<UGameplayAbility>> InDefaultAbilities) {
    DefaultAbilities = MoveTemp(InDefaultAbilities);
}

/**
 * @brief 配置重试间隔与初始化/复制最大重试次数（立即同步至内部闸门）
 * @param InRetryIntervalSeconds 重试时间间隔（秒，下限 0.01）
 * @param InMaxInitRetries 兜底初始化最大重试次数（下限 0）
 * @param InMaxReplicationRetries 等待复制就绪最大重试次数（下限 0）
 */
void UGameAbilitySystemBootstrap::ConfigureRetryPolicy(
    float InRetryIntervalSeconds,
    int32 InMaxInitRetries,
    int32 InMaxReplicationRetries
) {
    RetryIntervalSeconds = FMath::Max(MinRetryIntervalSeconds, InRetryIntervalSeconds);
    MaxInitRetries = FMath::Max(0, InMaxInitRetries);
    MaxReplicationRetries = FMath::Max(0, InMaxReplicationRetries);
    ConfigureRetryGates();
}

/**
 * @brief 统一幂等初始化入口（服务器与本地客户端均可调用，宿主握手钩子驱动）
 * @param InAvatar 当前附身的 Avatar
 */
void UGameAbilitySystemBootstrap::EnsureInitialized(AActor* InAvatar) {
    if (!ResolveAbilitySystemComponent.IsBound() || InAvatar == nullptr) {
        return;
    }

    ConfigureRetryGates();

    UAbilitySystemComponent* AbilitySystemComponent = ResolveAbilitySystemComponent.Execute();
    if (AbilitySystemComponent == nullptr) {
        return;
    }

    InitializeWithResolvedASC(AbilitySystemComponent, InAvatar);
}

/**
 * @brief 数据就绪通知（推进已完成 ActorInfo 握手的权威引导）
 */
void UGameAbilitySystemBootstrap::NotifyDataReady() {
    AdvanceAuthoritativeInitialization();
}

/**
 * @brief 兜底自愈入口（仅极端离线 / 重连脱节时触发，Avatar 或 ASC 暂未就绪均按限次重试）
 */
void UGameAbilitySystemBootstrap::TryInitializeFromOwner() {
    if (!ResolveAbilitySystemComponent.IsBound()) {
        return;
    }

    ConfigureRetryGates();

    APawn* Avatar = ResolveOwnerAvatar();
    if (Avatar == nullptr) {
        ScheduleInitRetry(TEXT("宿主尚未生成或附身有效 Avatar"));
        return;
    }

    const AActor* OwnerActor = GetOwner();
    if (OwnerActor != nullptr && !OwnerActor->HasAuthority() && !Avatar->IsLocallyControlled()) {
        return;
    }

    UAbilitySystemComponent* AbilitySystemComponent = ResolveAbilitySystemComponent.Execute();
    if (AbilitySystemComponent == nullptr) {
        ScheduleInitRetry(TEXT("宿主关联的 ASC 尚未就绪或未复制到达"));
        return;
    }

    InitializeWithResolvedASC(AbilitySystemComponent, Avatar);
}

/**
 * @brief 按组件配置同步初始化与 ASC 复制重试门的间隔和次数上限
 */
void UGameAbilitySystemBootstrap::ConfigureRetryGates() {
    ReplicationRetryGate.Configure(RetryIntervalSeconds, MaxReplicationRetries);
    InitRetryGate.Configure(RetryIntervalSeconds, MaxInitRetries);
}

/**
 * @brief 撤销本组件已授予给当前 ASC 的默认能力并清空句柄列表
 */
void UGameAbilitySystemBootstrap::ClearGrantedAbilities() {
    if (GrantedAbilityHandles.IsEmpty()) {
        return;
    }
    if (UAbilitySystemComponent* AbilitySystemComponent = CurrentASC.Get()) {
        if (AbilitySystemComponent->IsOwnerActorAuthoritative()) {
            for (const FGameplayAbilitySpecHandle& Handle : GrantedAbilityHandles) {
                AbilitySystemComponent->ClearAbility(Handle);
            }
        }
    }
    GrantedAbilityHandles.Empty();
}

/**
 * @brief 从组件拥有者解析兜底引导使用的 Pawn Avatar
 * @return 拥有者自身或其 PlayerState 关联的 Pawn；无法解析时返回 nullptr
 */
APawn* UGameAbilitySystemBootstrap::ResolveOwnerAvatar() const {
    AActor* OwnerActor = GetOwner();
    if (APawn* OwnerAsPawn = Cast<APawn>(OwnerActor)) {
        return OwnerAsPawn;
    }
    if (APlayerState* OwnerAsPlayerState = Cast<APlayerState>(OwnerActor)) {
        return OwnerAsPlayerState->GetPawn();
    }
    return nullptr;
}

/**
 * @brief 对已解析的非空 ASC 与 Avatar 执行幂等 ActorInfo 握手与权威阶段推进
 * @param InAbilitySystemComponent 已解析的非空 ASC
 * @param InAvatar 已解析的非空 Avatar
 */
void UGameAbilitySystemBootstrap::InitializeWithResolvedASC(UAbilitySystemComponent* InAbilitySystemComponent, AActor* InAvatar) {
    const bool bAscChanged = (CurrentASC.Get() != InAbilitySystemComponent);
    if (!bAscChanged && CurrentAvatar.Get() == InAvatar) {
        return;
    }

    if (bAscChanged) {
        ClearGrantedAbilities();
        CurrentASC = InAbilitySystemComponent;
        bInitialAttributesWritten = false;
        bDefaultAbilitiesGranted = false;
        ReplicationRetryGate.Reset();
        if (UWorld* World = GetWorld()) {
            World->GetTimerManager().ClearTimer(ReplicationRetryTimerHandle);
        }
    }

    AActor* EffectiveOwner = InAbilitySystemComponent->GetOwner();
    if (EffectiveOwner == nullptr) {
        EffectiveOwner = GetOwner();
    }
    if (EffectiveOwner == nullptr) {
        EffectiveOwner = InAvatar;
    }
    InAbilitySystemComponent->InitAbilityActorInfo(EffectiveOwner, InAvatar);
    CurrentAvatar = InAvatar;
    InitRetryGate.Reset();
    if (UWorld* World = GetWorld()) {
        World->GetTimerManager().ClearTimer(InitRetryTimerHandle);
    }

    AdvanceAuthoritativeInitialization();
}

/**
 * @brief 调度下一次兜底自愈定时器重试，超过次数上限则输出告警并中止
 * @param InTimeoutReason 超时日志中说明缺失对象的文本
 */
void UGameAbilitySystemBootstrap::ScheduleInitRetry(const TCHAR* InTimeoutReason) {
    UWorld* World = GetWorld();
    if (World == nullptr) {
        return;
    }
    if (InitRetryGate.Advance()) {
        World->GetTimerManager().SetTimer(
            InitRetryTimerHandle,
            this,
            &UGameAbilitySystemBootstrap::TryInitializeFromOwner,
            InitRetryGate.GetIntervalSeconds(),
            false
        );
    } else {
        GAME_GAS_LOG(
            LogGameAbility,
            Warning,
            TEXT("【GAS 引导】自愈重试超时（超过 %d 次），%s，自愈中止。"),
            InitRetryGate.GetMaxRetries(),
            InTimeoutReason
        );
    }
}

/**
 * @brief 在 ActorInfo 已初始化且数据就绪后写入初值并推进默认能力授予
 */
void UGameAbilitySystemBootstrap::AdvanceAuthoritativeInitialization() {
    UAbilitySystemComponent* AbilitySystemComponent = CurrentASC.Get();
    if (AbilitySystemComponent == nullptr || CurrentAvatar.Get() == nullptr) {
        return;
    }

    const AActor* AuthorityActor = GetOwner();
    if (AuthorityActor == nullptr) {
        AuthorityActor = AbilitySystemComponent->GetOwner();
    }
    if (AuthorityActor != nullptr && !AuthorityActor->HasAuthority()) {
        return;
    }

    const bool bAttributesComplete = bInitialAttributesWritten || !WriteInitialAttributes.IsBound();
    if (bAttributesComplete && bDefaultAbilitiesGranted) {
        return;
    }

    if (DataReadyQuery.IsBound() && !DataReadyQuery.Execute()) {
        return;
    }

    if (!bInitialAttributesWritten && WriteInitialAttributes.IsBound()) {
        WriteInitialAttributes.Execute(AbilitySystemComponent);
        bInitialAttributesWritten = true;
    }

    GrantDefaultAbilities();
}

/**
 * @brief 向当前已完成握手的 ASC 权威授予默认能力并跟踪句柄
 */
void UGameAbilitySystemBootstrap::GrantDefaultAbilities() {
    if (bDefaultAbilitiesGranted) {
        return;
    }

    UAbilitySystemComponent* AbilitySystemComponent = CurrentASC.Get();
    if (AbilitySystemComponent == nullptr) {
        return;
    }

    UWorld* World = GetWorld();
    if (DefaultAbilities.IsEmpty()) {
        if (World != nullptr) {
            World->GetTimerManager().ClearTimer(ReplicationRetryTimerHandle);
        }
        bDefaultAbilitiesGranted = true;
        return;
    }

    if (World != nullptr && !AbilitySystemComponent->IsReadyForReplication() && ReplicationRetryGate.Advance()) {
        World->GetTimerManager().SetTimer(
            ReplicationRetryTimerHandle,
            this,
            &UGameAbilitySystemBootstrap::GrantDefaultAbilities,
            ReplicationRetryGate.GetIntervalSeconds(),
            false
        );
        return;
    }
    if (World != nullptr) {
        World->GetTimerManager().ClearTimer(ReplicationRetryTimerHandle);
    }
    GAME_GAS_CLOG(
        LogGameAbility,
        World != nullptr && !AbilitySystemComponent->IsReadyForReplication(),
        Warning,
        TEXT("【GAS 引导】复制未在限次内就绪，已超限强制授予默认能力以保证初始化成功率。")
    );

    AActor* SourceActor = GetOwner() != nullptr ? GetOwner() : AbilitySystemComponent->GetOwner();
    for (const TSubclassOf<UGameplayAbility>& AbilityClass : DefaultAbilities) {
        if (AbilityClass == nullptr || AbilitySystemComponent->FindAbilitySpecFromClass(AbilityClass) != nullptr) {
            continue;
        }
        const FGameplayAbilitySpecHandle NewHandle = AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, SourceActor));
        if (NewHandle.IsValid()) {
            GrantedAbilityHandles.Add(NewHandle);
        }
    }
    bDefaultAbilitiesGranted = true;
}
