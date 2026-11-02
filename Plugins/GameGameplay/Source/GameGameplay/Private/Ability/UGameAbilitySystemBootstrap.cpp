#include "Ability/UGameAbilitySystemBootstrap.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"

#include "Logging/GameLogCategories.h"

UGameAbilitySystemBootstrap::UGameAbilitySystemBootstrap() {
    PrimaryComponentTick.bCanEverTick = false;
}

void UGameAbilitySystemBootstrap::BeginPlay() {
    Super::BeginPlay();
    ConfigureRetryGates();
    TryInitializeFromOwner();
}

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
    Super::EndPlay(EndPlayReason);
}

void UGameAbilitySystemBootstrap::SetResolveAbilitySystemComponentDelegate(const FResolveAbilitySystemComponent& InDelegate) {
    ResolveAbilitySystemComponent = InDelegate;
}

void UGameAbilitySystemBootstrap::SetWriteInitialAttributesDelegate(const FWriteInitialAttributes& InDelegate) {
    WriteInitialAttributes = InDelegate;
}

void UGameAbilitySystemBootstrap::SetDataReadyQueryDelegate(const FQueryDataReady& InDelegate) {
    DataReadyQuery = InDelegate;
}

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

void UGameAbilitySystemBootstrap::NotifyDataReady() {
    AdvanceAuthoritativeInitialization();
}

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

void UGameAbilitySystemBootstrap::ConfigureRetryGates() {
    ReplicationRetryGate.Configure(RetryIntervalSeconds, MaxReplicationRetries);
    InitRetryGate.Configure(RetryIntervalSeconds, MaxInitRetries);
}

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
        GrantedAbilityHandles.AddUnique(NewHandle);
    }
    bDefaultAbilitiesGranted = true;
}
