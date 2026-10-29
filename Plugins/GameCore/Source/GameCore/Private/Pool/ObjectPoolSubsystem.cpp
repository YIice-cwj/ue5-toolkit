// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pool/ObjectPoolSubsystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/AudioComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "GameFramework/MovementComponent.h"
#include "TimerManager.h"
#include "Pool/IGamePoolable.h"
#include "Time/TimeDilationSubsystem.h"
#include "Config/GameCompileSwitches.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameObjectPool, Log, All);

namespace {

void DeactivateActorForPool(AActor* Actor) {
    Actor->SetActorHiddenInGame(true);
    Actor->SetActorEnableCollision(false);
    Actor->SetActorTickEnabled(false);
}

void InvokePoolableCallback(AActor* Actor, bool bAcquire) {
    if (!Actor->GetClass()->ImplementsInterface(UGamePoolable::StaticClass())) {
        return;
    }
    IGamePoolable* NativePoolable = static_cast<IGamePoolable*>(
        Actor->GetNativeInterfaceAddress(UGamePoolable::StaticClass()));
    const bool bUseNative = NativePoolable != nullptr && Actor->GetClass()->HasAnyClassFlags(CLASS_Native);
    if (bAcquire) {
        if (bUseNative) {
            NativePoolable->OnAcquireFromPool_Implementation();
        } else {
            IGamePoolable::Execute_OnAcquireFromPool(Actor);
        }
    } else {
        if (bUseNative) {
            NativePoolable->OnReleaseToPool_Implementation();
        } else {
            IGamePoolable::Execute_OnReleaseToPool(Actor);
        }
    }
}

}  // namespace

UObjectPoolSubsystem& UObjectPoolSubsystem::Get(const UObject* WorldContextObject) {
    UObjectPoolSubsystem* Subsystem = GetPtr(WorldContextObject);
    check(Subsystem != nullptr);
    return *Subsystem;
}

UObjectPoolSubsystem* UObjectPoolSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (!WorldContextObject) {
        return nullptr;
    }
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)) {
        return World->GetSubsystem<UObjectPoolSubsystem>();
    }
    return nullptr;
}

void UObjectPoolSubsystem::Deinitialize() {
    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameObjectPool, Log, TEXT("【对象池】子系统注销，清空 %d 个对象池。"), PoolMap.Num());
    }
    ClearAllPools();
    Super::Deinitialize();
}

void UObjectPoolSubsystem::Prewarm(TSubclassOf<AActor> ActorClass, int32 PrewarmCount) {
    UWorld* World = GetWorld();
    if (!ActorClass || PrewarmCount <= 0) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameObjectPool, Warning, TEXT("【对象池】预热失败：ActorClass 为空或预热数量 <= 0。"));
        }
        return;
    }

    FPoolBucket& Bucket = PoolMap.FindOrAdd(ActorClass);
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    for (int32 Index = 0; Index < PrewarmCount; ++Index) {
        AActor* NewActor = World->SpawnActor<AActor>(ActorClass, FTransform::Identity, SpawnParams);
        if (NewActor) {
            DeactivateActorForPool(NewActor);
            Bucket.IdleActors.Add(NewActor);
            Bucket.IdleActorSet.Add(NewActor);
        }
    }

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameObjectPool, Log, TEXT("【对象池】预热完成: 类=[%s], 请求数量=%d, 当前空闲池大小=%d"),
            *ActorClass->GetName(), PrewarmCount, Bucket.IdleActors.Num());
    }
}

void UObjectPoolSubsystem::SetMaxPoolCapacity(TSubclassOf<AActor> ActorClass, int32 InMaxIdleCount) {
    if (!ActorClass) {
        return;
    }

    FPoolBucket& Bucket = PoolMap.FindOrAdd(ActorClass);
    Bucket.MaxIdleCount = InMaxIdleCount;
    while (InMaxIdleCount > 0 && Bucket.IdleActors.Num() > InMaxIdleCount) {
        TObjectPtr<AActor> ExcessActor = Bucket.IdleActors.Pop();
        Bucket.IdleActorSet.Remove(ExcessActor);
        if (IsValid(ExcessActor.Get())) {
            ExcessActor->OnDestroyed.RemoveAll(this);
            ExcessActor->Destroy();
        }
    }
}

AActor* UObjectPoolSubsystem::AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform) {
    UWorld* World = GetWorld();
    if (!ActorClass) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameObjectPool, Warning, TEXT("【对象池】借出 Actor 失败：ActorClass 为空。"));
        }
        return nullptr;
    }

    FPoolBucket& Bucket = PoolMap.FindOrAdd(ActorClass);
    AActor* ResultActor = nullptr;

    while (!Bucket.IdleActors.IsEmpty()) {
        TObjectPtr<AActor> Candidate = Bucket.IdleActors.Pop();
        Bucket.IdleActorSet.Remove(Candidate);
        if (IsValid(Candidate.Get())) {
            ResultActor = Candidate.Get();
            break;
        }
    }

    if (!ResultActor) {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ResultActor = World->SpawnActor<AActor>(ActorClass, SpawnTransform, SpawnParams);
    } else {
        ResultActor->SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::ResetPhysics);
        if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(ResultActor->GetRootComponent())) {
            RootPrim->SetPhysicsLinearVelocity(FVector::ZeroVector);
            RootPrim->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        }
    }

    if (!ResultActor) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameObjectPool, Error, TEXT("【对象池】生成 Actor 实例失败：类=[%s]"), *ActorClass->GetName());
        }
        return nullptr;
    }

    const AActor* DefaultActor = ActorClass->GetDefaultObject<AActor>();
    const bool bDefaultCollision = DefaultActor->GetActorEnableCollision();
    const bool bDefaultTickEnabled = DefaultActor->PrimaryActorTick.bCanEverTick && DefaultActor->PrimaryActorTick.bStartWithTickEnabled;
    const float DefaultTimeDilation = DefaultActor->CustomTimeDilation;

    ResultActor->SetActorHiddenInGame(false);
    ResultActor->SetActorEnableCollision(bDefaultCollision);
    ResultActor->SetActorTickEnabled(bDefaultTickEnabled);
    ResultActor->CustomTimeDilation = DefaultTimeDilation;

    ResultActor->OnDestroyed.AddUniqueDynamic(this, &UObjectPoolSubsystem::OnPooledActorDestroyed);
    InvokePoolableCallback(ResultActor, true);

    ++Bucket.ActiveCount;
    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameObjectPool, Verbose, TEXT("【对象池】借出 Actor 成功: 类=[%s], 实例=[%s], 活跃数=%d, 剩余空闲数=%d"),
            *ActorClass->GetName(), *GetNameSafe(ResultActor), Bucket.ActiveCount, Bucket.IdleActors.Num());
    }
    return ResultActor;
}

void UObjectPoolSubsystem::ReleaseActor(AActor* Actor) {
    if (!IsValid(Actor)) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameObjectPool, Warning, TEXT("【对象池】归还 Actor 失败：Actor 无效。"));
        }
        return;
    }

    TSubclassOf<AActor> ActorClass = Actor->GetClass();
    FPoolBucket& Bucket = PoolMap.FindOrAdd(ActorClass);
    if (Bucket.IdleActorSet.Contains(Actor)) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameObjectPool, Warning, TEXT("【对象池】归还警告：Actor [%s] 已在空闲池中，避免重复归还。"), *GetNameSafe(Actor));
        }
        return;
    }

    Actor->OnDestroyed.RemoveAll(this);
    InvokePoolableCallback(Actor, false);

    Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

    if (UTimeDilationSubsystem* TimeDilationSubsystem = UTimeDilationSubsystem::GetPtr(this)) {
        TimeDilationSubsystem->ClearHitStop(Actor);
    }

    Actor->GetWorldTimerManager().ClearAllTimersForObject(Actor);

    if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(Actor->GetRootComponent())) {
        RootPrim->SetPhysicsLinearVelocity(FVector::ZeroVector);
        RootPrim->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    }

    if (UMovementComponent* MovementComp = Actor->FindComponentByClass<UMovementComponent>()) {
        MovementComp->StopMovementImmediately();
    }

    TArray<UAudioComponent*> AudioComponents;
    Actor->GetComponents<UAudioComponent>(AudioComponents);
    for (UAudioComponent* AudioComp : AudioComponents) {
        if (AudioComp->IsPlaying()) {
            AudioComp->Stop();
        }
    }

    TArray<UParticleSystemComponent*> ParticleComponents;
    Actor->GetComponents<UParticleSystemComponent>(ParticleComponents);
    for (UParticleSystemComponent* ParticleComp : ParticleComponents) {
        if (ParticleComp->IsActive()) {
            ParticleComp->DeactivateSystem();
        }
    }

    DeactivateActorForPool(Actor);

    if (Bucket.MaxIdleCount > 0 && Bucket.IdleActors.Num() >= Bucket.MaxIdleCount) {
        Actor->Destroy();
    } else {
        Bucket.IdleActors.Add(Actor);
        Bucket.IdleActorSet.Add(Actor);
    }

    if (Bucket.ActiveCount > 0) {
        --Bucket.ActiveCount;
    }

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameObjectPool, Verbose, TEXT("【对象池】归还 Actor 成功: 类=[%s], 实例=[%s], 剩余活跃数=%d, 当前空闲数=%d"),
            *ActorClass->GetName(), *GetNameSafe(Actor), Bucket.ActiveCount, Bucket.IdleActors.Num());
    }
}

void UObjectPoolSubsystem::GetPoolStats(
    TSubclassOf<AActor> ActorClass,
    int32& OutTotalCount,
    int32& OutActiveCount,
    int32& OutIdleCount
) const {
    OutTotalCount = 0;
    OutActiveCount = 0;
    OutIdleCount = 0;

    if (const FPoolBucket* Bucket = PoolMap.Find(ActorClass)) {
        OutIdleCount = Bucket->IdleActors.Num();
        OutActiveCount = Bucket->ActiveCount;
        OutTotalCount = OutIdleCount + OutActiveCount;
    }
}

void UObjectPoolSubsystem::ClearPool(TSubclassOf<AActor> ActorClass) {
    if (FPoolBucket* Bucket = PoolMap.Find(ActorClass)) {
        const int32 Count = Bucket->IdleActors.Num();
        DestroyBucketIdleActors(*Bucket);
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameObjectPool, Log, TEXT("【对象池】清理对象池: 类=[%s], 销毁 Actor 数=%d"), *ActorClass->GetName(), Count);
        }
    }
}

void UObjectPoolSubsystem::ClearAllPools() {
    for (auto& Pair : PoolMap) {
        DestroyBucketIdleActors(Pair.Value);
    }
}

void UObjectPoolSubsystem::OnPooledActorDestroyed(AActor* DestroyedActor) {
    if (!DestroyedActor) {
        return;
    }

    TSubclassOf<AActor> ActorClass = DestroyedActor->GetClass();
    if (FPoolBucket* Bucket = PoolMap.Find(ActorClass)) {
        Bucket->IdleActorSet.Remove(DestroyedActor);
        Bucket->IdleActors.Remove(DestroyedActor);
        if (Bucket->ActiveCount > 0) {
            --Bucket->ActiveCount;
        }
    }
}

void UObjectPoolSubsystem::DestroyBucketIdleActors(FPoolBucket& Bucket) {
    for (TObjectPtr<AActor>& IdleActor : Bucket.IdleActors) {
        if (IsValid(IdleActor.Get())) {
            IdleActor->OnDestroyed.RemoveAll(this);
            IdleActor->Destroy();
        }
    }
    Bucket.IdleActors.Empty();
    Bucket.IdleActorSet.Empty();
}
