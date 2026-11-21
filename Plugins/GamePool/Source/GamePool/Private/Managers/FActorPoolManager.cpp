/**
 * @file FActorPoolManager.cpp
 * @brief Actor 对象池底层核心内部管理器实现
 */

#include "Managers/FActorPoolManager.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Core/PoolErrorCodes.h"
#include "Engine/World.h"
#include "Logging/GameLogCategories.h"
#include "Managers/FPooledActorResetter.h"
#include "Subsystem/UObjectPoolSubsystem.h"
#include "Telemetry/ObjectPoolStats.h"

namespace {

bool IsValidPoolableActorClass(const UClass* ActorClass) {
    return ActorClass != nullptr &&
           !ActorClass->HasAnyClassFlags(CLASS_Abstract) &&
           ActorClass->IsChildOf(AActor::StaticClass());
}

}  // namespace

/**
 * @brief 初始化 Actor 池管理器上下文
 * @param InWorld 所属 World 上下文
 * @param InGenTracker 世代追踪器指针
 * @param InSubsystemOwner 所属子系统对象指针
 */
void FActorPoolManager::Initialize(
    UWorld* InWorld,
    FPoolGenerationTracker* InGenTracker,
    UObjectPoolSubsystem* InSubsystemOwner
) {
    World = InWorld;
    GenerationTracker = InGenTracker;
    SubsystemOwner = InSubsystemOwner;
}

/**
 * @brief 反初始化管理器，清空全部空闲实例、平衡全部 Stat 计数并重置存储桶
 */
void FActorPoolManager::Deinitialize() {
    for (auto& Pair : PoolMap) {
        TPoolBucket<AActor>& Bucket = Pair.Value;
        DestroyBucketIdleActors(Bucket);

        const int32 ActiveCount = Bucket.ActiveSet.Num();
        if (ActiveCount <= 0) {
            continue;
        }
        if (SubsystemOwner != nullptr) {
            for (const TObjectPtr<AActor>& ActivePtr : Bucket.ActiveSet) {
                AActor* ActiveActor = ActivePtr.Get();
                if (IsValid(ActiveActor)) {
                    ActiveActor->OnDestroyed.RemoveAll(SubsystemOwner);
                }
            }
        }
        DEC_DWORD_STAT_BY(STAT_GamePool_ActiveActors, ActiveCount);
        Bucket.ActiveSet.Empty();
    }
    PoolMap.Empty();
    ResetTelemetry();
    World = nullptr;
    GenerationTracker = nullptr;
    SubsystemOwner = nullptr;
}

/**
 * @brief 同步预热指定类别的 Actor 到空闲池（受 MaxIdleCount 上限约束）
 * @param ActorClass 目标 Actor 类
 * @param PrewarmCount 预热生成的实例数量
 */
void FActorPoolManager::Prewarm(TSubclassOf<AActor> ActorClass, const int32 PrewarmCount) {
    UClass* RawClass = ActorClass.Get();
    if (!IsValidPoolableActorClass(RawClass) || PrewarmCount <= 0) {
        GAME_LOG(Warning, TEXT("【Actor对象池】预热失败：ActorClass 无效或预热数量 <= 0。"));
        return;
    }

    for (int32 Index = 0; Index < PrewarmCount; ++Index) {
        if (SpawnIdleActorToPool(ActorClass).IsFailure()) {
            break;
        }
    }

    const TPoolBucket<AActor>* Bucket = PoolMap.Find(RawClass);
    GAME_LOG(
        Log,
        TEXT("【Actor对象池】预热完成: 类=[%s], 请求数量=%d, 当前空闲池大小=%d"),
        *RawClass->GetName(),
        PrewarmCount,
        Bucket != nullptr ? Bucket->GetIdleCount() : 0
    );
}

/**
 * @brief 生成单个空闲 Actor 实例并放入空闲池（受 MaxIdleCount 上限约束）
 * @param ActorClass 目标 Actor 类
 * @return 成功生成并入池返回 Success，失败返回带 Game::ModuleId::Pool 的 Failure
 */
TGameStatusResult<void> FActorPoolManager::SpawnIdleActorToPool(TSubclassOf<AActor> ActorClass) {
    UClass* RawClass = ActorClass.Get();
    if (!IsValidPoolableActorClass(RawClass)) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidClass, TEXT("ActorClass is null or abstract"))
        );
    }
    if (World == nullptr) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidContext, TEXT("World context is null"))
        );
    }

    TPoolBucket<AActor>& Bucket = PoolMap.FindOrAdd(RawClass);
    if (Bucket.MaxIdleCount > 0 && Bucket.GetIdleCount() >= Bucket.MaxIdleCount) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::CapacityLimitReached, TEXT("Actor pool reached MaxIdleCount limit"))
        );
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* SpawnedActor = World->SpawnActor<AActor>(RawClass, FTransform::Identity, SpawnParams);
    if (SpawnedActor == nullptr) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::SpawnFailed, TEXT("Failed to spawn idle Actor instance"))
        );
    }

    if (SubsystemOwner != nullptr) {
        SpawnedActor->OnDestroyed.AddUniqueDynamic(SubsystemOwner, &UObjectPoolSubsystem::OnPooledActorDestroyed);
    }
    DeactivateActorForPool(SpawnedActor);
    Bucket.PushIdle(SpawnedActor, FPlatformTime::Seconds());
    INC_DWORD_STAT(STAT_GamePool_IdleActors);
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 设置指定 Actor 类的最大空闲缓存容量（超出部分按冷端优先立即裁剪销毁）
 * @param ActorClass 目标 Actor 类
 * @param InMaxIdleCount 最大空闲数
 */
void FActorPoolManager::SetMaxPoolCapacity(TSubclassOf<AActor> ActorClass, const int32 InMaxIdleCount) {
    UClass* RawClass = ActorClass.Get();
    if (!IsValidPoolableActorClass(RawClass)) {
        return;
    }

    TPoolBucket<AActor>& Bucket = PoolMap.FindOrAdd(RawClass);
    Bucket.MaxIdleCount = InMaxIdleCount;
    if (InMaxIdleCount <= 0) {
        return;
    }

    while (Bucket.GetIdleCount() > InMaxIdleCount) {
        TPoolBucket<AActor>::FIdleEntry ColdEntry;
        if (!Bucket.TryPopColdIdle(ColdEntry)) {
            break;
        }
        DEC_DWORD_STAT(STAT_GamePool_IdleActors);
        DestroyIdleActor(ColdEntry.Instance.Get());
    }
}

/**
 * @brief 配置指定 Actor 类别的组件自动重置策略掩码
 * @param ActorClass 目标 Actor 类
 * @param InResetFlags 重置位掩码
 */
void FActorPoolManager::SetPoolResetFlags(TSubclassOf<AActor> ActorClass, const EPoolResetFlags InResetFlags) {
    UClass* RawClass = ActorClass.Get();
    if (!IsValidPoolableActorClass(RawClass)) {
        return;
    }
    PoolMap.FindOrAdd(RawClass).ResetFlags = InResetFlags;
}

/**
 * @brief 查询指定 Actor 类别的组件自动重置策略掩码
 * @param ActorClass 目标 Actor 类
 * @return 当前配置的组件重置策略掩码
 */
EPoolResetFlags FActorPoolManager::GetPoolResetFlags(TSubclassOf<AActor> ActorClass) const {
    const UClass* RawClass = ActorClass.Get();
    if (RawClass == nullptr) {
        return EPoolResetFlags::All;
    }
    const TPoolBucket<AActor>* Bucket = PoolMap.Find(RawClass);
    return Bucket != nullptr ? Bucket->ResetFlags : EPoolResetFlags::All;
}

/**
 * @brief 配置指定 Actor 类别的闲置弹性缩容与淘汰策略
 * @param ActorClass 目标 Actor 类
 * @param InSettings 淘汰策略配置
 */
void FActorPoolManager::SetPoolEvictionSettings(TSubclassOf<AActor> ActorClass, const FPoolEvictionSettings& InSettings) {
    UClass* RawClass = ActorClass.Get();
    if (!IsValidPoolableActorClass(RawClass)) {
        return;
    }
    PoolMap.FindOrAdd(RawClass).EvictionSettings = InSettings;
}

/**
 * @brief 查询指定 Actor 类别的闲置弹性缩容与淘汰策略
 * @param ActorClass 目标 Actor 类
 * @return 当前配置的淘汰策略
 */
FPoolEvictionSettings FActorPoolManager::GetPoolEvictionSettings(TSubclassOf<AActor> ActorClass) const {
    const UClass* RawClass = ActorClass.Get();
    if (RawClass == nullptr) {
        return FPoolEvictionSettings();
    }
    const TPoolBucket<AActor>* Bucket = PoolMap.Find(RawClass);
    return Bucket != nullptr ? Bucket->EvictionSettings : FPoolEvictionSettings();
}

/**
 * @brief 计算所有启用淘汰的 Actor 池中的最小心跳检测周期
 * @param OutMinIntervalSeconds 传出最小心跳周期（秒）
 * @return 若至少有一个池启用淘汰返回 true，否则返回 false
 */
bool FActorPoolManager::GetMinimumEvictionCheckIntervalSeconds(float& OutMinIntervalSeconds) const noexcept {
    bool bFoundAny = false;
    float MinInterval = MAX_flt;
    for (const auto& Pair : PoolMap) {
        const FPoolEvictionSettings& Settings = Pair.Value.EvictionSettings;
        if (!Settings.bEnableEviction) {
            continue;
        }
        MinInterval = FMath::Min(MinInterval, FMath::Max(0.1f, Settings.CheckIntervalSeconds));
        bFoundAny = true;
    }
    if (bFoundAny) {
        OutMinIntervalSeconds = MinInterval;
    }
    return bFoundAny;
}

/**
 * @brief 执行超时冷 Actor 实例平滑淘汰（O(1) FIFO 从最冷端逐出）
 * @param NowSeconds 当前单调硬件时间戳（秒）
 * @param MaxGlobalQuota 本次全局最大销毁总配额
 * @param bRespectPerBucketLimit 是否同时受各池自身 MaxEvictionsPerStep 上限约束
 * @return 实际销毁的冷实例总数
 */
int32 FActorPoolManager::EvictExpiredActors(
    const double NowSeconds,
    const int32 MaxGlobalQuota,
    const bool bRespectPerBucketLimit
) {
    SCOPE_CYCLE_COUNTER(STAT_GamePool_EvictStep);
    TRACE_CPUPROFILER_EVENT_SCOPE(GamePool_EvictActors);

    if (MaxGlobalQuota <= 0) {
        return 0;
    }

    int32 TotalEvicted = 0;
    for (auto& Pair : PoolMap) {
        if (TotalEvicted >= MaxGlobalQuota) {
            break;
        }

        TPoolBucket<AActor>& Bucket = Pair.Value;
        const FPoolEvictionSettings& Settings = Bucket.EvictionSettings;
        if (!Settings.bEnableEviction) {
            continue;
        }

        const int32 MinPreserved = FMath::Max(0, Settings.MinPreservedCount);
        const int32 RemainingGlobal = MaxGlobalQuota - TotalEvicted;
        const int32 BucketQuota = bRespectPerBucketLimit
            ? FMath::Min(RemainingGlobal, FMath::Max(1, Settings.MaxEvictionsPerStep))
            : RemainingGlobal;

        int32 BucketEvicted = 0;
        while (BucketEvicted < BucketQuota && Bucket.GetIdleCount() > MinPreserved) {
            const double IdleSeconds = NowSeconds - Bucket.IdleEntries.First().ReturnTimeSeconds;
            if (IdleSeconds < Settings.IdleTimeToLiveSeconds) {
                break;
            }

            TPoolBucket<AActor>::FIdleEntry ColdEntry;
            Bucket.TryPopColdIdle(ColdEntry);
            DEC_DWORD_STAT(STAT_GamePool_IdleActors);
            DestroyIdleActor(ColdEntry.Instance.Get());
            ++BucketEvicted;
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
    const UClass* RawClass = ActorClass.Get();
    if (RawClass == nullptr) {
        return Stats;
    }
    if (const TPoolBucket<AActor>* Bucket = PoolMap.Find(RawClass)) {
        Stats.IdleCount = Bucket->GetIdleCount();
        Stats.ActiveCount = Bucket->GetActiveCount();
        Stats.TotalCount = Stats.IdleCount + Stats.ActiveCount;
    }
    return Stats;
}

/**
 * @brief 从池中借出或生成 Actor 实例（支持可选类型指纹载荷，并在全部回调后校验重入有效性）
 * @param ActorClass 目标 Actor 类
 * @param SpawnTransform 初始空间变换
 * @param Payload 可选业务载荷视图
 * @return 成功返回 Actor 原生指针，失败返回带 Game::ModuleId::Pool 的 Failure
 */
TGameStatusResult<AActor*> FActorPoolManager::AcquireActor(
    TSubclassOf<AActor> ActorClass,
    const FTransform& SpawnTransform,
    const FPoolAcquirePayload Payload
) {
    SCOPE_CYCLE_COUNTER(STAT_GamePool_AcquireActor);
    TRACE_CPUPROFILER_EVENT_SCOPE(GamePool_AcquireActor);

    UClass* RawClass = ActorClass.Get();
    if (!IsValidPoolableActorClass(RawClass)) {
        GAME_LOG(Warning, TEXT("【Actor对象池】借出 Actor 失败：ActorClass 无效或为抽象类。"));
        return TGameStatusResult<AActor*>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidClass, TEXT("ActorClass is null or abstract"))
        );
    }
    if (World == nullptr) {
        return TGameStatusResult<AActor*>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidContext, TEXT("World context is null"))
        );
    }

    AActor* ResultActor = nullptr;
    TPoolBucket<AActor>& Bucket = PoolMap.FindOrAdd(RawClass);

    TPoolBucket<AActor>::FIdleEntry HotEntry;
    while (Bucket.TryPopHotIdle(HotEntry)) {
        DEC_DWORD_STAT(STAT_GamePool_IdleActors);
        AActor* Candidate = HotEntry.Instance.Get();
        if (IsValid(Candidate)) {
            ResultActor = Candidate;
            break;
        }
        if (GenerationTracker != nullptr && Candidate != nullptr) {
            GenerationTracker->RemoveGeneration(Candidate);
        }
    }

    const bool bReused = (ResultActor != nullptr);
    if (!bReused) {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ResultActor = World->SpawnActor<AActor>(RawClass, SpawnTransform, SpawnParams);
        if (ResultActor == nullptr) {
            GAME_LOG(Error, TEXT("【Actor对象池】生成 Actor 实例失败：类=[%s]"), *RawClass->GetName());
            return TGameStatusResult<AActor*>::Failure(
                FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::SpawnFailed, TEXT("Failed to spawn Actor instance"))
            );
        }
        if (SubsystemOwner != nullptr) {
            ResultActor->OnDestroyed.AddUniqueDynamic(SubsystemOwner, &UObjectPoolSubsystem::OnPooledActorDestroyed);
        }
    } else {
        ResultActor->SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::ResetPhysics);
        if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(ResultActor->GetRootComponent())) {
            if (RootPrim->IsSimulatingPhysics()) {
                RootPrim->SetPhysicsLinearVelocity(FVector::ZeroVector);
                RootPrim->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
            }
        }
        const AActor* DefaultActor = RawClass->GetDefaultObject<AActor>();
        const bool bDefaultCollision = DefaultActor->GetActorEnableCollision();
        const bool bDefaultTickEnabled =
            DefaultActor->PrimaryActorTick.bCanEverTick && DefaultActor->PrimaryActorTick.bStartWithTickEnabled;

        ResultActor->SetActorHiddenInGame(false);
        ResultActor->SetActorEnableCollision(bDefaultCollision);
        ResultActor->SetActorTickEnabled(bDefaultTickEnabled);
        ResultActor->CustomTimeDilation = DefaultActor->CustomTimeDilation;
    }

    if (bReused) {
        ++CacheHits;
        INC_DWORD_STAT(STAT_GamePool_Hits);
    } else {
        ++CacheMisses;
        INC_DWORD_STAT(STAT_GamePool_Misses);
    }
    ++TotalAcquires;
    INC_DWORD_STAT(STAT_GamePool_ActiveActors);
    Bucket.ActiveSet.Add(ResultActor);

    if (GenerationTracker != nullptr) {
        GenerationTracker->GetOrIncrementGeneration(ResultActor);
    }

    FPooledActorResetter::InvokePoolableCallback(ResultActor, true);

    TPoolBucket<AActor>* PostAcquireBucket = PoolMap.Find(RawClass);
    const bool bValidAfterAcquire =
        PostAcquireBucket != nullptr && IsValid(ResultActor) && PostAcquireBucket->ActiveSet.Contains(ResultActor);
    if (bValidAfterAcquire && Payload.IsValid()) {
        FPooledActorResetter::InvokePoolablePayloadCallback(ResultActor, Payload);
        PostAcquireBucket = PoolMap.Find(RawClass);
    }

    if (PostAcquireBucket == nullptr || !IsValid(ResultActor) || !PostAcquireBucket->ActiveSet.Contains(ResultActor)) {
        if (PostAcquireBucket != nullptr && PostAcquireBucket->ActiveSet.Remove(ResultActor) > 0) {
            DEC_DWORD_STAT(STAT_GamePool_ActiveActors);
        }
        if (!IsValid(ResultActor) && GenerationTracker != nullptr) {
            GenerationTracker->RemoveGeneration(ResultActor);
        }
        return TGameStatusResult<AActor*>::Failure(
            FGameError::Make(
                Game::ModuleId::Pool,
                Game::PoolErrorCodes::InvalidatedDuringAcquire,
                TEXT("Actor became invalid or was released during acquire callback")
            )
        );
    }

    GAME_LOG(
        Verbose,
        TEXT("【Actor对象池】借出 Actor 成功: 类=[%s], 实例=[%s], 活跃数=%d, 剩余空闲数=%d"),
        *RawClass->GetName(),
        *GetNameSafe(ResultActor),
        PostAcquireBucket->GetActiveCount(),
        PostAcquireBucket->GetIdleCount()
    );
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
        GAME_LOG(Warning, TEXT("【Actor对象池】归还 Actor 失败：Actor 无效。"));
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidInstance, TEXT("Actor is null or pending kill"))
        );
    }

    UClass* RawClass = Actor->GetClass();
    TPoolBucket<AActor>* Bucket = PoolMap.Find(RawClass);
    if (Bucket == nullptr || Bucket->ActiveSet.Remove(Actor) == 0) {
        GAME_LOG_THROTTLE(
            1.0,
            Warning,
            TEXT("【Actor对象池】归还警告：Actor [%s] 不是当前活跃池属实例，已忽略归还。"),
            *GetNameSafe(Actor)
        );
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::NotOwnedActiveInstance, TEXT("Actor is not an active member of this pool"))
        );
    }
    DEC_DWORD_STAT(STAT_GamePool_ActiveActors);

    if (GenerationTracker != nullptr) {
        GenerationTracker->InvalidateGeneration(Actor);
    }
    const EPoolResetFlags ResetFlags = Bucket->ResetFlags;

    FPooledActorResetter::InvokePoolableCallback(Actor, false);
    if (!IsValid(Actor)) {
        if (GenerationTracker != nullptr) {
            GenerationTracker->RemoveGeneration(Actor);
        }
        return TGameStatusResult<void>::Success();
    }

    Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

    const USceneComponent* RootComp = Actor->GetRootComponent();
    if (RootComp != nullptr && !RootComp->GetAttachChildren().IsEmpty()) {
        TArray<AActor*> AttachedChildren;
        Actor->GetAttachedActors(AttachedChildren);
        for (AActor* ChildActor : AttachedChildren) {
            if (IsValid(ChildActor)) {
                ChildActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
            }
        }
    }

    Actor->CustomTimeDilation = 1.0f;
    FPooledActorResetter::ResetActorComponents(Actor, ResetFlags);
    if (!IsValid(Actor)) {
        if (GenerationTracker != nullptr) {
            GenerationTracker->RemoveGeneration(Actor);
        }
        return TGameStatusResult<void>::Success();
    }

    DeactivateActorForPool(Actor);

    TPoolBucket<AActor>* PostReleaseBucket = PoolMap.Find(RawClass);
    if (PostReleaseBucket == nullptr ||
        (PostReleaseBucket->MaxIdleCount > 0 && PostReleaseBucket->GetIdleCount() >= PostReleaseBucket->MaxIdleCount)) {
        DestroyIdleActor(Actor);
        GAME_LOG(
            Verbose,
            TEXT("【Actor对象池】Actor 超出容量上限已销毁: 类=[%s], 实例=[%s]"),
            *RawClass->GetName(),
            *GetNameSafe(Actor)
        );
    } else {
        PostReleaseBucket->PushIdle(Actor, FPlatformTime::Seconds());
        INC_DWORD_STAT(STAT_GamePool_IdleActors);
        GAME_LOG(
            Verbose,
            TEXT("【Actor对象池】归还 Actor 成功: 类=[%s], 实例=[%s], 剩余活跃数=%d, 当前空闲数=%d"),
            *RawClass->GetName(),
            *GetNameSafe(Actor),
            PostReleaseBucket->GetActiveCount(),
            PostReleaseBucket->GetIdleCount()
        );
    }
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 清空并销毁指定 Actor 类别的所有空闲实例
 * @param ActorClass 目标 Actor 类
 */
void FActorPoolManager::ClearPool(TSubclassOf<AActor> ActorClass) {
    UClass* RawClass = ActorClass.Get();
    if (RawClass == nullptr) {
        return;
    }
    if (TPoolBucket<AActor>* Bucket = PoolMap.Find(RawClass)) {
        const int32 Count = Bucket->GetIdleCount();
        DestroyBucketIdleActors(*Bucket);
        GAME_LOG(Log, TEXT("【Actor对象池】清理对象池: 类=[%s], 销毁 Actor 数=%d"), *RawClass->GetName(), Count);
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
 * @brief 响应池化 Actor 外部意外销毁的自愈回调（优先 O(1) 活跃集合移除并同步扣减 Stat）
 * @param DestroyedActor 被销毁的 Actor 实例
 */
void FActorPoolManager::OnPooledActorDestroyed(AActor* DestroyedActor) {
    if (DestroyedActor == nullptr) {
        return;
    }

    if (GenerationTracker != nullptr) {
        GenerationTracker->RemoveGeneration(DestroyedActor);
    }

    TPoolBucket<AActor>* Bucket = PoolMap.Find(DestroyedActor->GetClass());
    if (Bucket == nullptr) {
        return;
    }

    if (Bucket->ActiveSet.Remove(DestroyedActor) > 0) {
        DEC_DWORD_STAT(STAT_GamePool_ActiveActors);
        return;
    }

    if (Bucket->RemoveIdleInstance(DestroyedActor)) {
        DEC_DWORD_STAT(STAT_GamePool_IdleActors);
    }
}

/**
 * @brief 泄洪销毁所有类别的空闲 Actor 实例（从最冷端逐出，优先保留最热保底实例）
 * @param bPreserveMinCount 是否保留各池配置的 MinPreservedCount 保底数量
 * @return 实际销毁的实例数
 */
int32 FActorPoolManager::FlushIdleActors(const bool bPreserveMinCount) {
    int32 TotalFlushed = 0;
    for (auto& Pair : PoolMap) {
        TPoolBucket<AActor>& Bucket = Pair.Value;
        const int32 PreservedLimit = bPreserveMinCount ? FMath::Max(0, Bucket.EvictionSettings.MinPreservedCount) : 0;

        while (Bucket.GetIdleCount() > PreservedLimit) {
            TPoolBucket<AActor>::FIdleEntry ColdEntry;
            Bucket.TryPopColdIdle(ColdEntry);
            DEC_DWORD_STAT(STAT_GamePool_IdleActors);
            DestroyIdleActor(ColdEntry.Instance.Get());
            ++TotalFlushed;
        }
    }
    return TotalFlushed;
}

/**
 * @brief 查询指定 Actor 类别空闲池是否已达 MaxIdleCount 容量上限
 * @param ActorClass 目标 Actor 类
 * @return 已达上限返回 true
 */
bool FActorPoolManager::IsAtIdleCapacity(TSubclassOf<AActor> ActorClass) const {
    const UClass* RawClass = ActorClass.Get();
    if (RawClass == nullptr) {
        return false;
    }
    const TPoolBucket<AActor>* Bucket = PoolMap.Find(RawClass);
    return Bucket != nullptr && Bucket->MaxIdleCount > 0 && Bucket->GetIdleCount() >= Bucket->MaxIdleCount;
}

/**
 * @brief 向 UE 垃圾回收系统注册全部 Class Key、空闲与活跃 Actor 强引用
 * @param Collector 引用收集器
 * @param ReferencingObject 引用宿主对象
 */
void FActorPoolManager::AddReferencedObjects(FReferenceCollector& Collector, const UObject* ReferencingObject) {
    for (auto& Pair : PoolMap) {
        Collector.AddReferencedObject(Pair.Key, ReferencingObject);
        Pair.Value.AddReferencedObjects(Collector, ReferencingObject);
    }
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
        Sum += Pair.Value.GetIdleCount();
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

/**
 * @brief 销毁单个空闲池 Actor 并清理其世代记录与销毁委托
 * @param Actor 待销毁的 Actor 指针
 */
void FActorPoolManager::DestroyIdleActor(AActor* Actor) {
    if (Actor == nullptr) {
        return;
    }
    if (GenerationTracker != nullptr) {
        GenerationTracker->RemoveGeneration(Actor);
    }
    if (!IsValid(Actor)) {
        return;
    }
    if (SubsystemOwner != nullptr) {
        Actor->OnDestroyed.RemoveAll(SubsystemOwner);
    }
    Actor->Destroy();
}

/**
 * @brief 销毁指定存储桶中的所有空闲 Actor 实例并同步扣减空闲 Stat
 * @param Bucket 目标 Actor 存储桶
 */
void FActorPoolManager::DestroyBucketIdleActors(TPoolBucket<AActor>& Bucket) {
    const int32 IdleCount = Bucket.GetIdleCount();
    if (IdleCount <= 0) {
        return;
    }

    TPoolBucket<AActor>::FIdleEntry Entry;
    while (Bucket.TryPopColdIdle(Entry)) {
        DestroyIdleActor(Entry.Instance.Get());
    }
    DEC_DWORD_STAT_BY(STAT_GamePool_IdleActors, IdleCount);
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
