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
#include "TimerManager.h"

#include "Logging/GameLogCategories.h"

/**
 * @brief 构造函数：关闭组件 Tick（引导全程由定时器与握手事件驱动）
 */
UGameAbilitySystemBootstrap::UGameAbilitySystemBootstrap() {
    PrimaryComponentTick.bCanEverTick = false;
}

/**
 * @brief 组件启动生命周期回调：配置重试闸门并尝试兜底引导
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
    if (UWorld* World = GetWorld()) {
        World->GetTimerManager().ClearTimer(InitRetryTimerHandle);
        World->GetTimerManager().ClearTimer(ReplicationRetryTimerHandle);
    }
    if (UAbilitySystemComponent* AbilitySystemComponent = CurrentASC.Get()) {
        for (const FGameplayAbilitySpecHandle& Handle : GrantedAbilityHandles) {
            AbilitySystemComponent->ClearAbility(Handle);
        }
    }
    GrantedAbilityHandles.Empty();
    if (HasBegunPlay()) {
        Super::EndPlay(EndPlayReason);
    }
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

    const bool bAscChanged = (CurrentASC.Get() != AbilitySystemComponent);
    if (!bAscChanged && CurrentAvatar.Get() == InAvatar) {
        return;
    }

    if (bAscChanged) {
        if (UAbilitySystemComponent* OldASC = CurrentASC.Get()) {
            for (const FGameplayAbilitySpecHandle& Handle : GrantedAbilityHandles) {
                OldASC->ClearAbility(Handle);
            }
        }
        GrantedAbilityHandles.Empty();
        CurrentASC = AbilitySystemComponent;
        bInitialAttributesWritten = false;
        bDefaultAbilitiesGranted = false;
        ReplicationRetryGate.Reset();
        if (UWorld* World = GetWorld()) {
            World->GetTimerManager().ClearTimer(ReplicationRetryTimerHandle);
        }
    }

    AActor* EffectiveOwner = AbilitySystemComponent->GetOwner();
    if (EffectiveOwner == nullptr) {
        EffectiveOwner = GetOwner();
    }
    if (EffectiveOwner == nullptr) {
        EffectiveOwner = InAvatar;
    }
    AbilitySystemComponent->InitAbilityActorInfo(EffectiveOwner, InAvatar);
    CurrentAvatar = InAvatar;
    InitRetryGate.Reset();
    if (UWorld* World = GetWorld()) {
        World->GetTimerManager().ClearTimer(InitRetryTimerHandle);
    }

    AdvanceAuthoritativeInitialization();
}

/**
 * @brief 数据就绪通知（推进已完成 ActorInfo 握手的权威引导）
 */
void UGameAbilitySystemBootstrap::NotifyDataReady() {
    AdvanceAuthoritativeInitialization();
}

/**
 * @brief 兜底自愈入口（仅极端离线 / 重连脱节时触发）
 */
void UGameAbilitySystemBootstrap::TryInitializeFromOwner() {
    if (!ResolveAbilitySystemComponent.IsBound()) {
        return;
    }

    APawn* Avatar = ResolveOwnerAvatar();
    UAbilitySystemComponent* AbilitySystemComponent = ResolveAbilitySystemComponent.Execute();
    if (AbilitySystemComponent != nullptr && CurrentASC.Get() == AbilitySystemComponent && Avatar != nullptr && CurrentAvatar.Get() == Avatar) {
        return;
    }

    if (Avatar == nullptr) {
        UWorld* World = GetWorld();
        if (World != nullptr) {
            if (InitRetryGate.Advance()) {
                World->GetTimerManager().SetTimer(InitRetryTimerHandle, this, &UGameAbilitySystemBootstrap::TryInitializeFromOwner, InitRetryGate.GetIntervalSeconds(), false);
            } else {
                GAME_LOG(Warning, TEXT("【GAS 引导】自愈重试超时（超过 %d 次），宿主尚未生成或附身有效 Avatar，自愈中止。"), InitRetryGate.GetMaxRetries());
            }
        }
        return;
    }

    const AActor* OwnerActor = GetOwner();
    if (OwnerActor != nullptr && !OwnerActor->HasAuthority() && !Avatar->IsLocallyControlled()) {
        return;
    }

    EnsureInitialized(Avatar);
}

/**
 * @brief 按组件配置设置初始化与 ASC 复制重试门的间隔和次数上限
 */
void UGameAbilitySystemBootstrap::ConfigureRetryGates() {
    ReplicationRetryGate.Configure(RetryIntervalSeconds, MaxReplicationRetries);
    InitRetryGate.Configure(RetryIntervalSeconds, MaxInitRetries);
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

    if (bInitialAttributesWritten && bDefaultAbilitiesGranted) {
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

    if (DefaultAbilities.IsEmpty()) {
        if (UWorld* World = GetWorld()) {
            World->GetTimerManager().ClearTimer(ReplicationRetryTimerHandle);
        }
        bDefaultAbilitiesGranted = true;
        return;
    }

    UWorld* World = GetWorld();
    if (!AbilitySystemComponent->IsReadyForReplication() && ReplicationRetryGate.Advance()) {
        if (World != nullptr) {
            World->GetTimerManager().SetTimer(ReplicationRetryTimerHandle, this, &UGameAbilitySystemBootstrap::GrantDefaultAbilities, ReplicationRetryGate.GetIntervalSeconds(), false);
        }
        return;
    }
    if (World != nullptr) {
        World->GetTimerManager().ClearTimer(ReplicationRetryTimerHandle);
    }
    GAME_CLOG(!AbilitySystemComponent->IsReadyForReplication(), Warning, TEXT("【GAS 引导】复制未在限次内就绪，已超限强制授予默认能力以保证初始化成功率。"));

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
