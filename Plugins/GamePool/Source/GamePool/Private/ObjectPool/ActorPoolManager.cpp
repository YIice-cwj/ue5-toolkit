// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectPool/ActorPoolManager.h"
#include "ObjectPool/ObjectPoolSubsystem.h"
#include "ObjectPool/PooledActorResetter.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Config/GameCompileSwitches.h"

namespace {

DEFINE_LOG_CATEGORY_STATIC(LogGameActorPool, Log, All);

}  // namespace

/**
 * @brief 初始化 Actor 池管理器
 * @param InWorld 所属 World 上下文
 * @param InGenTracker 世代追踪器指针
 * @param InSubsystemOwner 所属子系统对象指针
 */
void FActorPoolManager::Initialize(UWorld* InWorld, FPoolGenerationTracker* InGenTracker, UObjectPoolSubsystem* InSubsystemOwner) {
    World = InWorld;
    GenerationTracker = InGenTracker;
    SubsystemOwner = InSubsystemOwner;
}

/**
 * @brief 同步预热指定类别的 Actor
 * @param ActorClass 目标 Actor 类
 * @param PrewarmCount 预热生成的实例数量
 */
void FActorPoolManager::Prewarm(TSubclassOf<AActor> ActorClass, int32 PrewarmCount) {
    if (!ActorClass || ActorClass->HasAnyClassFlags(CLASS_Abstract) || PrewarmCount <= 0) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameActorPool, Warning, TEXT("【Actor对象池】预热失败：ActorClass 无效或预热数量 <= 0。"));
        }
        return;
    }

    for (int32 Index = 0; Index < PrewarmCount; ++Index) {
        SpawnIdleActorToPool(ActorClass);
    }

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameActorPool, Log, TEXT("【Actor对象池】预热完成: 类=[%s], 请求数量=%d, 当前空闲池大小=%d"),
            *ActorClass->GetName(), PrewarmCount, PoolMap.FindOrAdd(ActorClass).IdleActors.Num());
    }
}

/**
 * @brief 生成单个空闲 Actor 实例并放入空闲池
 * @param ActorClass 目标 Actor 类
 * @return 成功生成并入池返回 true
 */
bool FActorPoolManager::SpawnIdleActorToPool(TSubclassOf<AActor> ActorClass) {
    if (!ActorClass || ActorClass->HasAnyClassFlags(CLASS_Abstract) || World == nullptr) {
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* SpawnedActor = World->SpawnActor<AActor>(ActorClass, FTransform::Identity, SpawnParams);
    if (!SpawnedActor) {
        return false;
    }

    DeactivateActorForPool(SpawnedActor);
    FPoolBucket& Bucket = PoolMap.FindOrAdd(ActorClass);
    Bucket.IdleActors.Add(SpawnedActor);
    Bucket.IdleActorSet.Add(SpawnedActor);
    return true;
}

/**
 * @brief 设置指定 Actor 类的最大空闲缓存容量
 * @param ActorClass 目标 Actor 类
 * @param InMaxIdleCount 最大空闲数
 */
void FActorPoolManager::SetMaxPoolCapacity(TSubclassOf<AActor> ActorClass, int32 InMaxIdleCount) {
    if (!ActorClass) {
        return;
    }

    FPoolBucket& Bucket = PoolMap.FindOrAdd(ActorClass);
    Bucket.MaxIdleCount = InMaxIdleCount;
    if (InMaxIdleCount > 0 && Bucket.IdleActors.Num() > InMaxIdleCount) {
        const int32 TrimCount = Bucket.IdleActors.Num() - InMaxIdleCount;
        for (int32 Index = 0; Index < TrimCount; ++Index) {
            TObjectPtr<AActor> ActorToDestroy = Bucket.IdleActors.Pop();
            Bucket.IdleActorSet.Remove(ActorToDestroy);
            if (GenerationTracker != nullptr) {
                GenerationTracker->InvalidateGeneration(ActorToDestroy);
            }
            if (IsValid(ActorToDestroy.Get())) {
                ActorToDestroy->Destroy();
            }
        }
    }
}

/**
 * @brief 配置指定 Actor 类别的组件自动重置策略掩码
 * @param ActorClass 目标 Actor 类
 * @param InResetFlags 重置位掩码
 */
void FActorPoolManager::SetPoolResetFlags(TSubclassOf<AActor> ActorClass, EPoolResetFlags InResetFlags) {
    if (!ActorClass) {
        return;
    }
    PoolMap.FindOrAdd(ActorClass).ResetFlags = InResetFlags;
}

/**
 * @brief 查询指定 Actor 类别的组件自动重置策略掩码
 * @param ActorClass 目标 Actor 类
 * @return 当前配置的组件重置策略掩码
 */
EPoolResetFlags FActorPoolManager::GetPoolResetFlags(TSubclassOf<AActor> ActorClass) const {
    if (!ActorClass) {
        return EPoolResetFlags::All;
    }
    const FPoolBucket* Bucket = PoolMap.Find(ActorClass);
    return Bucket != nullptr ? Bucket->ResetFlags : EPoolResetFlags::All;
}

/**
 * @brief 查询指定 Actor 类别的池容量与活跃统计快照
 * @param ActorClass 目标 Actor 类
 * @return 池状态快照结构体
 */
FPoolStats FActorPoolManager::GetPoolStats(TSubclassOf<AActor> ActorClass) const {
    FPoolStats Stats;
    if (ActorClass != nullptr) {
        if (const FPoolBucket* Bucket = PoolMap.Find(ActorClass)) {
            Stats.IdleCount = Bucket->IdleActors.Num();
            Stats.ActiveCount = Bucket->ActiveCount;
            Stats.TotalCount = Stats.IdleCount + Stats.ActiveCount;
        }
    }
    return Stats;
}

/**
 * @brief 从池中借出或生成 Actor 实例
 * @param ActorClass 目标 Actor 类
 * @param SpawnTransform 初始空间变换
 * @return 成功返回 Actor 原生指针，失败返回 Failure
 */
TGameStatusResult<AActor*> FActorPoolManager::AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform) {
    if (!ActorClass || ActorClass->HasAnyClassFlags(CLASS_Abstract) || World == nullptr) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameActorPool, Warning, TEXT("【Actor对象池】借出 Actor 失败：ActorClass 或世界上下文无效。"));
        }
        return TGameStatusResult<AActor*>::Failure(5001, TEXT("ActorClass or World is invalid"));
    }

    AActor* ResultActor = nullptr;

    while (true) {
        TObjectPtr<AActor> Candidate;
        {
            FPoolBucket& Bucket = PoolMap.FindOrAdd(ActorClass);
            if (Bucket.IdleActors.IsEmpty()) {
                break;
            }
            Candidate = Bucket.IdleActors.Pop();
            Bucket.IdleActorSet.Remove(Candidate);
        }
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
            UE_LOG(LogGameActorPool, Error, TEXT("【Actor对象池】生成 Actor 实例失败：类=[%s]"), *ActorClass->GetName());
        }
        return TGameStatusResult<AActor*>::Failure(5002, TEXT("Failed to spawn Actor"));
    }

    const AActor* DefaultActor = ActorClass->GetDefaultObject<AActor>();
    const bool bDefaultCollision = DefaultActor->GetActorEnableCollision();
    const bool bDefaultTickEnabled = DefaultActor->PrimaryActorTick.bCanEverTick && DefaultActor->PrimaryActorTick.bStartWithTickEnabled;
    const float DefaultTimeDilation = DefaultActor->CustomTimeDilation;

    ResultActor->SetActorHiddenInGame(false);
    ResultActor->SetActorEnableCollision(bDefaultCollision);
    ResultActor->SetActorTickEnabled(bDefaultTickEnabled);
    ResultActor->CustomTimeDilation = DefaultTimeDilation;

    if (SubsystemOwner != nullptr) {
        ResultActor->OnDestroyed.AddUniqueDynamic(SubsystemOwner, &UObjectPoolSubsystem::OnPooledActorDestroyed);
    }

    {
        FPoolBucket& Bucket = PoolMap.FindOrAdd(ActorClass);
        Bucket.ActiveActorSet.Add(ResultActor);
        ++Bucket.ActiveCount;
    }

    if (GenerationTracker != nullptr) {
        GenerationTracker->GetOrIncrementGeneration(ResultActor);
    }

    FPooledActorResetter::InvokePoolableCallback(ResultActor, true);
    const FPoolBucket* Bucket = PoolMap.Find(ActorClass);
    if (!IsValid(ResultActor) || Bucket == nullptr || !Bucket->ActiveActorSet.Contains(ResultActor)) {
        if (GenerationTracker != nullptr) {
            GenerationTracker->InvalidateGeneration(ResultActor);
        }
        return TGameStatusResult<AActor*>::Failure(5002, TEXT("Actor was invalidated during OnAcquireFromPool"));
    }

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameActorPool, Verbose, TEXT("【Actor对象池】借出 Actor 成功: 类=[%s], 实例=[%s], 活跃数=%d, 剩余空闲数=%d"),
            *ActorClass->GetName(), *GetNameSafe(ResultActor), Bucket->ActiveCount, Bucket->IdleActors.Num());
    }
    return TGameStatusResult<AActor*>::Success(ResultActor);
}

/**
 * @brief 将已借出的 Actor 归还回池休眠
 * @param Actor 待归还的实例
 * @return 成功归还返回 Success，未借出或已销毁返回 Failure
 */
TGameStatusResult<void> FActorPoolManager::ReleaseActor(AActor* Actor) {
    if (!IsValid(Actor)) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameActorPool, Warning, TEXT("【Actor对象池】归还 Actor 失败：Actor 无效。"));
        }
        return TGameStatusResult<void>::Failure(5004, TEXT("Actor is invalid or already destroyed"));
    }

    TSubclassOf<AActor> ActorClass = Actor->GetClass();
    FPoolBucket* Bucket = PoolMap.Find(ActorClass);
    if (Bucket == nullptr || Bucket->ActiveActorSet.Remove(Actor) == 0) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameActorPool, Warning, TEXT("【Actor对象池】归还警告：Actor [%s] 不是当前活跃池属实例，已忽略归还。"), *GetNameSafe(Actor));
        }
        return TGameStatusResult<void>::Failure(5003, TEXT("Actor is not an active member of this pool"));
    }
    --Bucket->ActiveCount;

    if (GenerationTracker != nullptr) {
        GenerationTracker->InvalidateGeneration(Actor);
    }
    const EPoolResetFlags ResetFlags = Bucket->ResetFlags;

    if (SubsystemOwner != nullptr) {
        Actor->OnDestroyed.RemoveAll(SubsystemOwner);
    }
    FPooledActorResetter::InvokePoolableCallback(Actor, false);
    if (!IsValid(Actor)) {
        return TGameStatusResult<void>::Success();
    }

    Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

    TArray<AActor*> AttachedChildren;
    Actor->GetAttachedActors(AttachedChildren);
    for (AActor* ChildActor : AttachedChildren) {
        if (IsValid(ChildActor)) {
            ChildActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
        }
    }

    Actor->CustomTimeDilation = 1.0f;

    FPooledActorResetter::ResetActorComponents(Actor, ResetFlags);

    if (!IsValid(Actor)) {
        return TGameStatusResult<void>::Success();
    }
    DeactivateActorForPool(Actor);

    bool bDestroyActor = false;
    {
        FPoolBucket& CurrentBucket = PoolMap.FindOrAdd(ActorClass);
        if (CurrentBucket.MaxIdleCount > 0 && CurrentBucket.IdleActors.Num() >= CurrentBucket.MaxIdleCount) {
            bDestroyActor = true;
        } else {
            CurrentBucket.IdleActors.Add(Actor);
            CurrentBucket.IdleActorSet.Add(Actor);
        }
    }
    if (bDestroyActor) {
        if (GenerationTracker != nullptr) {
            GenerationTracker->InvalidateGeneration(Actor);
        }
        Actor->Destroy();
    }

    if constexpr (Game::Config::bEnableLog) {
        const FPoolBucket* CurrentBucket = PoolMap.Find(ActorClass);
        UE_LOG(LogGameActorPool, Verbose, TEXT("【Actor对象池】归还 Actor 成功: 类=[%s], 实例=[%s], 剩余活跃数=%d, 当前空闲数=%d"),
            *ActorClass->GetName(), *GetNameSafe(Actor), CurrentBucket->ActiveCount, CurrentBucket->IdleActors.Num());
    }
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 清空并销毁指定 Actor 类别的所有空闲实例
 * @param ActorClass 目标 Actor 类
 */
void FActorPoolManager::ClearPool(TSubclassOf<AActor> ActorClass) {
    if (FPoolBucket* Bucket = PoolMap.Find(ActorClass)) {
        const int32 Count = Bucket->IdleActors.Num();
        DestroyBucketIdleActors(*Bucket);
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameActorPool, Log, TEXT("【Actor对象池】清理对象池: 类=[%s], 销毁 Actor 数=%d"), *ActorClass->GetName(), Count);
        }
    }
}

/**
 * @brief 清空并销毁所有类别的空闲 Actor 实例
 */
void FActorPoolManager::ClearAllPools() {
    TArray<TSubclassOf<AActor>> ActorClasses;
    PoolMap.GenerateKeyArray(ActorClasses);
    for (const TSubclassOf<AActor>& ActorClass : ActorClasses) {
        if (FPoolBucket* Bucket = PoolMap.Find(ActorClass)) {
            DestroyBucketIdleActors(*Bucket);
        }
    }
}

/**
 * @brief 响应池化 Actor 外部意外销毁的回调
 * @param DestroyedActor 被销毁的 Actor 实例
 */
void FActorPoolManager::OnPooledActorDestroyed(AActor* DestroyedActor) {
    if (!DestroyedActor) {
        return;
    }

    if (GenerationTracker != nullptr) {
        GenerationTracker->InvalidateGeneration(DestroyedActor);
    }

    TSubclassOf<AActor> ActorClass = DestroyedActor->GetClass();
    if (FPoolBucket* Bucket = PoolMap.Find(ActorClass)) {
        Bucket->IdleActorSet.Remove(DestroyedActor);
        Bucket->IdleActors.Remove(DestroyedActor);
        if (Bucket->ActiveActorSet.Remove(DestroyedActor) > 0) {
            --Bucket->ActiveCount;
        }
    }
}

/**
 * @brief 销毁指定存储桶中的所有空闲 Actor 实例
 * @param Bucket 目标 Actor 存储桶
 */
void FActorPoolManager::DestroyBucketIdleActors(FPoolBucket& Bucket) {
    for (const TObjectPtr<AActor>& ActorPtr : Bucket.IdleActors) {
        if (AActor* Actor = ActorPtr.Get()) {
            if (GenerationTracker != nullptr) {
                GenerationTracker->InvalidateGeneration(Actor);
            }
            if (IsValid(Actor)) {
                if (SubsystemOwner != nullptr) {
                    Actor->OnDestroyed.RemoveAll(SubsystemOwner);
                }
                Actor->Destroy();
            }
        }
    }
    Bucket.IdleActors.Empty();
    Bucket.IdleActorSet.Empty();
}

/**
 * @brief 将 Actor 设置为对象池休眠态（关闭渲染、碰撞与 Tick）
 * @param Actor 待休眠实例
 */
void FActorPoolManager::DeactivateActorForPool(AActor* Actor) {
    if (!IsValid(Actor)) {
        return;
    }
    Actor->SetActorHiddenInGame(true);
    Actor->SetActorEnableCollision(false);
    Actor->SetActorTickEnabled(false);
}
