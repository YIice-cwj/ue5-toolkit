// Copyright Epic Games, Inc. All Rights Reserved.

#include "Managers/ActorPoolManager.h"
#include "Telemetry/ObjectPoolStats.h"
#include "Subsystem/ObjectPoolSubsystem.h"
#include "Managers/PooledActorResetter.h"
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
 * @return 成功生成并入池返回 Success，失败返回 Failure
 */
TGameStatusResult<void> FActorPoolManager::SpawnIdleActorToPool(TSubclassOf<AActor> ActorClass) {
    if (!ActorClass) {
        return TGameStatusResult<void>::Failure(1, TEXT("ActorClass 为空"));
    }
    if (ActorClass->HasAnyClassFlags(CLASS_Abstract)) {
        return TGameStatusResult<void>::Failure(2, TEXT("ActorClass 为抽象类"));
    }
    if (World == nullptr) {
        return TGameStatusResult<void>::Failure(3, TEXT("World 上下文为空"));
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* SpawnedActor = World->SpawnActor<AActor>(ActorClass, FTransform::Identity, SpawnParams);
    if (!SpawnedActor) {
        return TGameStatusResult<void>::Failure(4, TEXT("生成 Actor 实例失败"));
    }

    DeactivateActorForPool(SpawnedActor);
    FPoolBucket& Bucket = PoolMap.FindOrAdd(ActorClass);
    Bucket.IdleActors.Add(SpawnedActor);
    Bucket.IdleReturnTimestamps.Add(FPlatformTime::Seconds());
    Bucket.IdleActorSet.Add(SpawnedActor);
    INC_DWORD_STAT(STAT_GamePool_IdleActors);
    return TGameStatusResult<void>::Success();
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
            if (Bucket.IdleReturnTimestamps.Num() > 0) {
                Bucket.IdleReturnTimestamps.Pop();
            }
            Bucket.IdleActorSet.Remove(ActorToDestroy);
            DEC_DWORD_STAT(STAT_GamePool_IdleActors);
            if (GenerationTracker != nullptr) {
                GenerationTracker->RemoveGeneration(ActorToDestroy.Get());
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
 * @brief 配置指定 Actor 类别的闲置弹性缩容与淘汰策略
 * @param ActorClass 目标 Actor 类
 * @param InSettings 淘汰策略配置
 */
void FActorPoolManager::SetPoolEvictionSettings(TSubclassOf<AActor> ActorClass, const FPoolEvictionSettings& InSettings) {
    if (!ActorClass) {
        return;
    }
    PoolMap.FindOrAdd(ActorClass).EvictionSettings = InSettings;
}

/**
 * @brief 查询指定 Actor 类别的闲置弹性缩容与淘汰策略
 * @param ActorClass 目标 Actor 类
 * @return 当前配置的淘汰策略
 */
FPoolEvictionSettings FActorPoolManager::GetPoolEvictionSettings(TSubclassOf<AActor> ActorClass) const {
    if (const FPoolBucket* Bucket = PoolMap.Find(ActorClass)) {
        return Bucket->EvictionSettings;
    }
    return FPoolEvictionSettings();
}

/**
 * @brief 执行超时冷实例淘汰
 * @param NowSeconds 当前单调硬件时间戳（秒）
 * @param MaxEvictCount 单次最大销毁配额（防止单步 GC 顿卡）
 * @return 实际销毁的冷实例总数
 */
int32 FActorPoolManager::EvictExpiredActors(const double NowSeconds, const int32 MaxEvictCount) {
    SCOPE_CYCLE_COUNTER(STAT_GamePool_EvictStep);
    TRACE_CPUPROFILER_EVENT_SCOPE(GamePool_EvictActors);

    if (MaxEvictCount <= 0) {
        return 0;
    }

    int32 TotalEvicted = 0;
    for (auto& Pair : PoolMap) {
        FPoolBucket& Bucket = Pair.Value;
        if (!Bucket.EvictionSettings.bEnableEviction) {
            continue;
        }

        const float Ttl = Bucket.EvictionSettings.IdleTimeToLiveSeconds;
        const int32 MinPreserved = FMath::Max(0, Bucket.EvictionSettings.MinPreservedCount);

        while (TotalEvicted < MaxEvictCount &&
               Bucket.IdleActors.Num() > MinPreserved &&
               Bucket.IdleReturnTimestamps.Num() > 0) {
            const double IdleTime = NowSeconds - Bucket.IdleReturnTimestamps[0];
            if (IdleTime < Ttl) {
                break;
            }

            TObjectPtr<AActor> ActorToDestroy = Bucket.IdleActors[0];
            Bucket.IdleActors.RemoveAt(0);
            Bucket.IdleReturnTimestamps.RemoveAt(0);
            Bucket.IdleActorSet.Remove(ActorToDestroy);
            DEC_DWORD_STAT(STAT_GamePool_IdleActors);

            if (AActor* RawActor = ActorToDestroy.Get()) {
                if (GenerationTracker != nullptr) {
                    GenerationTracker->RemoveGeneration(RawActor);
                }
                if (IsValid(RawActor)) {
                    if (SubsystemOwner != nullptr) {
                        RawActor->OnDestroyed.RemoveAll(SubsystemOwner);
                    }
                    RawActor->Destroy();
                }
            }
            ++TotalEvicted;
        }
    }
    return TotalEvicted;
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
            Stats.ActiveCount = Bucket->GetActiveCount();
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
    SCOPE_CYCLE_COUNTER(STAT_GamePool_AcquireActor);
    TRACE_CPUPROFILER_EVENT_SCOPE(GamePool_AcquireActor);

    if (!ActorClass || ActorClass->HasAnyClassFlags(CLASS_Abstract) || World == nullptr) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameActorPool, Warning, TEXT("【Actor对象池】借出 Actor 失败：ActorClass 或世界上下文无效。"));
        }
        return TGameStatusResult<AActor*>::Failure(5001, TEXT("ActorClass 或 World 上下文无效"));
    }

    AActor* ResultActor = nullptr;
    FPoolBucket& Bucket = PoolMap.FindOrAdd(ActorClass);

    while (!Bucket.IdleActors.IsEmpty()) {
        TObjectPtr<AActor> Candidate = Bucket.IdleActors.Pop();
        if (Bucket.IdleReturnTimestamps.Num() > 0) {
            Bucket.IdleReturnTimestamps.Pop();
        }
        Bucket.IdleActorSet.Remove(Candidate);
        if (IsValid(Candidate.Get())) {
            ResultActor = Candidate.Get();
            break;
        }
    }

    const bool bReused = (ResultActor != nullptr);
    if (bReused) {
        ++CacheHits;
        INC_DWORD_STAT(STAT_GamePool_Hits);
        DEC_DWORD_STAT(STAT_GamePool_IdleActors);
    } else {
        ++CacheMisses;
        INC_DWORD_STAT(STAT_GamePool_Misses);
    }
    ++TotalAcquires;

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
        return TGameStatusResult<AActor*>::Failure(5002, TEXT("生成 Actor 实例失败"));
    }

    INC_DWORD_STAT(STAT_GamePool_ActiveActors);

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

    Bucket.ActiveActorSet.Add(ResultActor);

    if (GenerationTracker != nullptr) {
        GenerationTracker->GetOrIncrementGeneration(ResultActor);
    }

    FPooledActorResetter::InvokePoolableCallback(ResultActor, true);
    if (!IsValid(ResultActor) || !Bucket.ActiveActorSet.Contains(ResultActor)) {
        if (GenerationTracker != nullptr) {
            GenerationTracker->RemoveGeneration(ResultActor);
        }
        if (Bucket.ActiveActorSet.Remove(ResultActor) > 0) {
            DEC_DWORD_STAT(STAT_GamePool_ActiveActors);
        }
        return TGameStatusResult<AActor*>::Failure(5002, TEXT("Actor 在 OnAcquireFromPool 回调中失效"));
    }

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameActorPool, Verbose, TEXT("【Actor对象池】借出 Actor 成功: 类=[%s], 实例=[%s], 活跃数=%d, 剩余空闲数=%d"),
            *ActorClass->GetName(), *GetNameSafe(ResultActor), Bucket.GetActiveCount(), Bucket.IdleActors.Num());
    }
    return TGameStatusResult<AActor*>::Success(ResultActor);
}

/**
 * @brief 将已借出的 Actor 归还回池休眠
 * @param Actor 待归还的实例
 * @return 成功归还返回 Success，未借出或已销毁返回 Failure
 */
TGameStatusResult<void> FActorPoolManager::ReleaseActor(AActor* Actor) {
    SCOPE_CYCLE_COUNTER(STAT_GamePool_ReleaseActor);
    TRACE_CPUPROFILER_EVENT_SCOPE(GamePool_ReleaseActor);

    if (!IsValid(Actor)) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameActorPool, Warning, TEXT("【Actor对象池】归还 Actor 失败：Actor 无效。"));
        }
        return TGameStatusResult<void>::Failure(5004, TEXT("Actor 无效或已被销毁"));
    }

    TSubclassOf<AActor> ActorClass = Actor->GetClass();
    FPoolBucket* Bucket = PoolMap.Find(ActorClass);
    if (Bucket == nullptr || Bucket->ActiveActorSet.Remove(Actor) == 0) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameActorPool, Warning, TEXT("【Actor对象池】归还警告：Actor [%s] 不是当前活跃池属实例，已忽略归还。"), *GetNameSafe(Actor));
        }
        return TGameStatusResult<void>::Failure(5003, TEXT("Actor 不是此对象池的活跃借出成员"));
    }
    DEC_DWORD_STAT(STAT_GamePool_ActiveActors);

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

    if (Bucket->MaxIdleCount > 0 && Bucket->IdleActors.Num() >= Bucket->MaxIdleCount) {
        if (GenerationTracker != nullptr) {
            GenerationTracker->RemoveGeneration(Actor);
        }
        Actor->Destroy();
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameActorPool, Verbose, TEXT("【Actor对象池】Actor 超出容量上限已销毁: 类=[%s], 实例=[%s]"),
                *ActorClass->GetName(), *GetNameSafe(Actor));
        }
    } else {
        Bucket->IdleActors.Add(Actor);
        Bucket->IdleReturnTimestamps.Add(FPlatformTime::Seconds());
        Bucket->IdleActorSet.Add(Actor);
        INC_DWORD_STAT(STAT_GamePool_IdleActors);
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameActorPool, Verbose, TEXT("【Actor对象池】归还 Actor 成功: 类=[%s], 实例=[%s], 剩余活跃数=%d, 当前空闲数=%d"),
                *ActorClass->GetName(), *GetNameSafe(Actor), Bucket->GetActiveCount(), Bucket->IdleActors.Num());
        }
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
    for (auto& Pair : PoolMap) {
        DestroyBucketIdleActors(Pair.Value);
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
        GenerationTracker->RemoveGeneration(DestroyedActor);
    }

    TSubclassOf<AActor> ActorClass = DestroyedActor->GetClass();
    if (FPoolBucket* Bucket = PoolMap.Find(ActorClass)) {
        Bucket->IdleActorSet.Remove(DestroyedActor);
        const int32 Index = Bucket->IdleActors.IndexOfByKey(DestroyedActor);
        if (Index != INDEX_NONE) {
            Bucket->IdleActors.RemoveAt(Index);
            if (Bucket->IdleReturnTimestamps.IsValidIndex(Index)) {
                Bucket->IdleReturnTimestamps.RemoveAt(Index);
            }
        }
        Bucket->ActiveActorSet.Remove(DestroyedActor);
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
                GenerationTracker->RemoveGeneration(Actor);
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
    Bucket.IdleReturnTimestamps.Empty();
    Bucket.IdleActorSet.Empty();
}

/**
 * @brief 泄洪销毁所有类别的空闲 Actor 实例
 * @param bPreserveMinCount 是否保留各池配置的保底留存数量
 * @return 实际销毁的实例数
 */
int32 FActorPoolManager::FlushIdleActors(const bool bPreserveMinCount) {
    int32 TotalFlushed = 0;
    for (auto& Pair : PoolMap) {
        FPoolBucket& Bucket = Pair.Value;
        const int32 PreservedLimit = (bPreserveMinCount && Bucket.EvictionSettings.bEnableEviction)
            ? FMath::Max(0, Bucket.EvictionSettings.MinPreservedCount)
            : 0;

        while (Bucket.IdleActors.Num() > PreservedLimit) {
            TObjectPtr<AActor> ActorPtr = Bucket.IdleActors.Pop();
            if (Bucket.IdleReturnTimestamps.Num() > 0) {
                Bucket.IdleReturnTimestamps.Pop();
            }
            Bucket.IdleActorSet.Remove(ActorPtr);
            DEC_DWORD_STAT(STAT_GamePool_IdleActors);

            if (AActor* Actor = ActorPtr.Get()) {
                if (GenerationTracker != nullptr) {
                    GenerationTracker->RemoveGeneration(Actor);
                }
                if (IsValid(Actor)) {
                    if (SubsystemOwner != nullptr) {
                        Actor->OnDestroyed.RemoveAll(SubsystemOwner);
                    }
                    Actor->Destroy();
                }
            }
            ++TotalFlushed;
        }
    }
    return TotalFlushed;
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

/**
 * @brief 获取全部类别的活跃 Actor 实例总数
 * @return 活跃 Actor 实例总数
 */
int32 FActorPoolManager::GetTotalActiveCount() const noexcept {
    int32 Sum = 0;
    for (const auto& Pair : PoolMap) {
        Sum += Pair.Value.GetActiveCount();
    }
    return Sum;
}

/**
 * @brief 获取全部类别的空闲 Actor 实例总数
 * @return 空闲 Actor 实例总数
 */
int32 FActorPoolManager::GetTotalIdleCount() const noexcept {
    int32 Sum = 0;
    for (const auto& Pair : PoolMap) {
        Sum += Pair.Value.IdleActors.Num();
    }
    return Sum;
}

/**
 * @brief 重置遥测统计计数器
 */
void FActorPoolManager::ResetTelemetry() noexcept {
    TotalAcquires = 0;
    CacheHits = 0;
    CacheMisses = 0;
}
