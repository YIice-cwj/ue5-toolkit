/**
 * @file FUObjectPoolManager.cpp
 * @brief 纯 UObject 对象池底层核心内部管理器实现
 */

#include "Managers/FUObjectPoolManager.h"

#include "Core/PoolErrorCodes.h"
#include "GameFramework/Actor.h"
#include "Logging/GameLogCategories.h"
#include "Managers/FPooledActorResetter.h"
#include "Telemetry/ObjectPoolStats.h"

namespace {

bool IsValidPoolableUObjectClass(const UClass* ObjectClass) {
    return ObjectClass != nullptr &&
           !ObjectClass->HasAnyClassFlags(CLASS_Abstract) &&
           !ObjectClass->IsChildOf(AActor::StaticClass());
}

}  // namespace

/**
 * @brief 初始化 UObject 池管理器
 * @param InOuter 所属 Outer 对象
 * @param InGenTracker 世代代数追踪器指针
 */
void FUObjectPoolManager::Initialize(UObject* InOuter, FPoolGenerationTracker* InGenTracker) {
    OuterObject = InOuter;
    GenerationTracker = InGenTracker;
}

/**
 * @brief 反初始化管理器，清理全部空闲实例、平衡全部 Stat 计数并清空存储桶
 */
void FUObjectPoolManager::Deinitialize() {
    for (auto& Pair : ObjectPoolMap) {
        TPoolBucket<UObject>& Bucket = Pair.Value;
        DestroyBucketIdleObjects(Bucket);

        const int32 ActiveCount = Bucket.ActiveSet.Num();
        if (ActiveCount > 0) {
            DEC_DWORD_STAT_BY(STAT_GamePool_ActiveObjects, ActiveCount);
            Bucket.ActiveSet.Empty();
        }
    }
    ObjectPoolMap.Empty();
    ResetTelemetry();
    OuterObject = nullptr;
    GenerationTracker = nullptr;
}

/**
 * @brief 同步预热指定纯 UObject 类别（受 MaxIdleCount 上限约束）
 * @param ObjectClass 目标 UObject 类
 * @param PrewarmCount 预热数量
 */
void FUObjectPoolManager::Prewarm(TSubclassOf<UObject> ObjectClass, const int32 PrewarmCount) {
    UClass* RawClass = ObjectClass.Get();
    if (!IsValidPoolableUObjectClass(RawClass) || PrewarmCount <= 0 || OuterObject == nullptr) {
        GAME_LOG(Warning, TEXT("【UObject对象池】预热失败：ObjectClass 无效或预热数量 <= 0。"));
        return;
    }

    for (int32 Index = 0; Index < PrewarmCount; ++Index) {
        if (SpawnIdleObjectToPool(ObjectClass).IsFailure()) {
            break;
        }
    }

    const TPoolBucket<UObject>* Bucket = ObjectPoolMap.Find(RawClass);
    GAME_LOG(
        Log,
        TEXT("【UObject对象池】预热完成: 类=[%s], 请求数量=%d, 当前空闲池大小=%d"),
        *RawClass->GetName(),
        PrewarmCount,
        Bucket != nullptr ? Bucket->GetIdleCount() : 0
    );
}

/**
 * @brief 生成单个空闲 UObject 实例并放入空闲池（受 MaxIdleCount 上限约束）
 * @param ObjectClass 目标类
 * @return 成功生成并入池返回 Success，失败返回带 Game::ModuleId::Pool 的 Failure
 */
TGameStatusResult<void> FUObjectPoolManager::SpawnIdleObjectToPool(TSubclassOf<UObject> ObjectClass) {
    UClass* RawClass = ObjectClass.Get();
    if (!IsValidPoolableUObjectClass(RawClass)) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidClass, TEXT("ObjectClass is null, abstract, or derives from AActor"))
        );
    }
    if (OuterObject == nullptr) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidContext, TEXT("OuterObject context is null"))
        );
    }

    TPoolBucket<UObject>& Bucket = ObjectPoolMap.FindOrAdd(RawClass);
    if (Bucket.MaxIdleCount > 0 && Bucket.GetIdleCount() >= Bucket.MaxIdleCount) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::CapacityLimitReached, TEXT("UObject pool reached MaxIdleCount limit"))
        );
    }

    UObject* NewInstance = NewObject<UObject>(OuterObject.Get(), RawClass);
    if (NewInstance == nullptr) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::SpawnFailed, TEXT("Failed to instantiate idle UObject"))
        );
    }

    Bucket.PushIdle(NewInstance, FPlatformTime::Seconds());
    INC_DWORD_STAT(STAT_GamePool_IdleObjects);
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 设置指定 UObject 类的最大空闲缓存容量（超出部分按冷端优先立即裁剪回收）
 * @param ObjectClass 目标类
 * @param InMaxIdleCount 最大空闲数
 */
void FUObjectPoolManager::SetMaxPoolCapacity(TSubclassOf<UObject> ObjectClass, const int32 InMaxIdleCount) {
    UClass* RawClass = ObjectClass.Get();
    if (!IsValidPoolableUObjectClass(RawClass)) {
        return;
    }

    TPoolBucket<UObject>& Bucket = ObjectPoolMap.FindOrAdd(RawClass);
    Bucket.MaxIdleCount = InMaxIdleCount;
    if (InMaxIdleCount <= 0) {
        return;
    }

    while (Bucket.GetIdleCount() > InMaxIdleCount) {
        TPoolBucket<UObject>::FIdleEntry ColdEntry;
        if (!Bucket.TryPopColdIdle(ColdEntry)) {
            break;
        }
        DEC_DWORD_STAT(STAT_GamePool_IdleObjects);
        DiscardIdleObject(ColdEntry.Instance.Get());
    }
}

/**
 * @brief 配置指定纯 UObject 类别的闲置弹性缩容与淘汰策略
 * @param ObjectClass 目标类
 * @param InSettings 淘汰策略配置
 */
void FUObjectPoolManager::SetPoolEvictionSettings(TSubclassOf<UObject> ObjectClass, const FPoolEvictionSettings& InSettings) {
    UClass* RawClass = ObjectClass.Get();
    if (!IsValidPoolableUObjectClass(RawClass)) {
        return;
    }
    ObjectPoolMap.FindOrAdd(RawClass).EvictionSettings = InSettings;
}

/**
 * @brief 查询指定纯 UObject 类别的闲置弹性缩容与淘汰策略
 * @param ObjectClass 目标类
 * @return 当前配置的淘汰策略
 */
FPoolEvictionSettings FUObjectPoolManager::GetPoolEvictionSettings(TSubclassOf<UObject> ObjectClass) const {
    const UClass* RawClass = ObjectClass.Get();
    if (RawClass == nullptr) {
        return FPoolEvictionSettings();
    }
    const TPoolBucket<UObject>* Bucket = ObjectPoolMap.Find(RawClass);
    return Bucket != nullptr ? Bucket->EvictionSettings : FPoolEvictionSettings();
}

/**
 * @brief 计算所有启用淘汰的 UObject 池中的最小心跳检测周期
 * @param OutMinIntervalSeconds 传出最小心跳周期（秒）
 * @return 若至少有一个池启用淘汰返回 true，否则返回 false
 */
bool FUObjectPoolManager::GetMinimumEvictionCheckIntervalSeconds(float& OutMinIntervalSeconds) const noexcept {
    bool bFoundAny = false;
    float MinInterval = MAX_flt;
    for (const auto& Pair : ObjectPoolMap) {
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
 * @brief 执行超时冷 UObject 实例平滑淘汰（O(1) FIFO 从最冷端逐出）
 * @param NowSeconds 当前单调硬件时间戳（秒）
 * @param MaxGlobalQuota 本次全局最大销毁总配额
 * @param bRespectPerBucketLimit 是否同时受各池自身 MaxEvictionsPerStep 上限约束
 * @return 实际销毁的冷实例总数
 */
int32 FUObjectPoolManager::EvictExpiredObjects(
    const double NowSeconds,
    const int32 MaxGlobalQuota,
    const bool bRespectPerBucketLimit
) {
    SCOPE_CYCLE_COUNTER(STAT_GamePool_EvictStep);
    TRACE_CPUPROFILER_EVENT_SCOPE(GamePool_EvictObjects);

    if (MaxGlobalQuota <= 0) {
        return 0;
    }

    int32 TotalEvicted = 0;
    for (auto& Pair : ObjectPoolMap) {
        if (TotalEvicted >= MaxGlobalQuota) {
            break;
        }

        TPoolBucket<UObject>& Bucket = Pair.Value;
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

            TPoolBucket<UObject>::FIdleEntry ColdEntry;
            Bucket.TryPopColdIdle(ColdEntry);
            DEC_DWORD_STAT(STAT_GamePool_IdleObjects);
            DiscardIdleObject(ColdEntry.Instance.Get());
            ++BucketEvicted;
            ++TotalEvicted;
        }
    }
    return TotalEvicted;
}

/**
 * @brief 查询指定纯 UObject 类的池容量与活跃统计快照
 * @param ObjectClass 目标类
 * @return 池状态统计结构体
 */
FPoolStats FUObjectPoolManager::GetPoolStats(TSubclassOf<UObject> ObjectClass) const {
    FPoolStats Stats;
    const UClass* RawClass = ObjectClass.Get();
    if (RawClass == nullptr) {
        return Stats;
    }
    if (const TPoolBucket<UObject>* Bucket = ObjectPoolMap.Find(RawClass)) {
        Stats.IdleCount = Bucket->GetIdleCount();
        Stats.ActiveCount = Bucket->GetActiveCount();
        Stats.TotalCount = Stats.IdleCount + Stats.ActiveCount;
    }
    return Stats;
}

/**
 * @brief 从池中借出或构建纯 UObject 实例（支持可选类型指纹载荷，并在全部回调后校验重入有效性）
 * @param ObjectClass 目标类
 * @param Payload 可选业务载荷视图
 * @return 成功返回 UObject 指针，失败返回带 Game::ModuleId::Pool 的 Failure
 */
TGameStatusResult<UObject*> FUObjectPoolManager::AcquireObject(
    TSubclassOf<UObject> ObjectClass,
    const FPoolAcquirePayload Payload
) {
    SCOPE_CYCLE_COUNTER(STAT_GamePool_AcquireObject);
    TRACE_CPUPROFILER_EVENT_SCOPE(GamePool_AcquireObject);

    UClass* RawClass = ObjectClass.Get();
    if (!IsValidPoolableUObjectClass(RawClass)) {
        GAME_LOG(Warning, TEXT("【UObject对象池】借出 UObject 失败：ObjectClass 无效或属于 AActor 派生类。"));
        return TGameStatusResult<UObject*>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidClass, TEXT("ObjectClass is null, abstract, or derives from AActor"))
        );
    }
    if (OuterObject == nullptr) {
        return TGameStatusResult<UObject*>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidContext, TEXT("OuterObject context is null"))
        );
    }

    UObject* ResultObject = nullptr;
    TPoolBucket<UObject>& Bucket = ObjectPoolMap.FindOrAdd(RawClass);

    TPoolBucket<UObject>::FIdleEntry HotEntry;
    while (Bucket.TryPopHotIdle(HotEntry)) {
        DEC_DWORD_STAT(STAT_GamePool_IdleObjects);
        UObject* Candidate = HotEntry.Instance.Get();
        if (IsValid(Candidate)) {
            ResultObject = Candidate;
            break;
        }
        if (GenerationTracker != nullptr && Candidate != nullptr) {
            GenerationTracker->RemoveGeneration(Candidate);
        }
    }

    const bool bReused = (ResultObject != nullptr);
    if (!bReused) {
        ResultObject = NewObject<UObject>(OuterObject.Get(), RawClass);
        if (ResultObject == nullptr) {
            return TGameStatusResult<UObject*>::Failure(
                FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::SpawnFailed, TEXT("Failed to instantiate UObject"))
            );
        }
    }

    if (bReused) {
        ++CacheHits;
        INC_DWORD_STAT(STAT_GamePool_Hits);
    } else {
        ++CacheMisses;
        INC_DWORD_STAT(STAT_GamePool_Misses);
    }
    ++TotalAcquires;
    INC_DWORD_STAT(STAT_GamePool_ActiveObjects);

    Bucket.ActiveSet.Add(ResultObject);
    if (GenerationTracker != nullptr) {
        GenerationTracker->GetOrIncrementGeneration(ResultObject);
    }

    FPooledActorResetter::InvokePoolableCallback(ResultObject, true);

    TPoolBucket<UObject>* PostAcquireBucket = ObjectPoolMap.Find(RawClass);
    const bool bValidAfterAcquire =
        PostAcquireBucket != nullptr && IsValid(ResultObject) && PostAcquireBucket->ActiveSet.Contains(ResultObject);
    if (bValidAfterAcquire && Payload.IsValid()) {
        FPooledActorResetter::InvokePoolablePayloadCallback(ResultObject, Payload);
        PostAcquireBucket = ObjectPoolMap.Find(RawClass);
    }

    if (PostAcquireBucket == nullptr || !IsValid(ResultObject) || !PostAcquireBucket->ActiveSet.Contains(ResultObject)) {
        if (PostAcquireBucket != nullptr && PostAcquireBucket->ActiveSet.Remove(ResultObject) > 0) {
            DEC_DWORD_STAT(STAT_GamePool_ActiveObjects);
        }
        if (!IsValid(ResultObject) && GenerationTracker != nullptr) {
            GenerationTracker->RemoveGeneration(ResultObject);
        }
        return TGameStatusResult<UObject*>::Failure(
            FGameError::Make(
                Game::ModuleId::Pool,
                Game::PoolErrorCodes::InvalidatedDuringAcquire,
                TEXT("UObject became invalid or was released during acquire callback")
            )
        );
    }

    return TGameStatusResult<UObject*>::Success(ResultObject);
}

/**
 * @brief 将已借出的纯 UObject 归还回池
 * @param Object 待归还的实例
 * @return 成功归还返回 Success，未借出或已销毁返回 Failure
 */
TGameStatusResult<void> FUObjectPoolManager::ReleaseObject(UObject* Object) {
    SCOPE_CYCLE_COUNTER(STAT_GamePool_ReleaseObject);
    TRACE_CPUPROFILER_EVENT_SCOPE(GamePool_ReleaseObject);

    if (!IsValid(Object)) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidInstance, TEXT("UObject is null or marked as garbage"))
        );
    }

    UClass* RawClass = Object->GetClass();
    TPoolBucket<UObject>* Bucket = ObjectPoolMap.Find(RawClass);
    if (Bucket == nullptr || Bucket->ActiveSet.Remove(Object) == 0) {
        GAME_LOG_THROTTLE(
            1.0,
            Warning,
            TEXT("【UObject对象池】归还警告：UObject [%s] 不是当前活跃池属实例，已忽略归还。"),
            *GetNameSafe(Object)
        );
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::NotOwnedActiveInstance, TEXT("UObject is not an active member of this pool"))
        );
    }
    DEC_DWORD_STAT(STAT_GamePool_ActiveObjects);

    if (GenerationTracker != nullptr) {
        GenerationTracker->InvalidateGeneration(Object);
    }

    FPooledActorResetter::InvokePoolableCallback(Object, false);
    if (!IsValid(Object)) {
        if (GenerationTracker != nullptr) {
            GenerationTracker->RemoveGeneration(Object);
        }
        return TGameStatusResult<void>::Success();
    }

    TPoolBucket<UObject>* PostReleaseBucket = ObjectPoolMap.Find(RawClass);
    if (PostReleaseBucket == nullptr ||
        (PostReleaseBucket->MaxIdleCount > 0 && PostReleaseBucket->GetIdleCount() >= PostReleaseBucket->MaxIdleCount)) {
        DiscardIdleObject(Object);
    } else {
        PostReleaseBucket->PushIdle(Object, FPlatformTime::Seconds());
        INC_DWORD_STAT(STAT_GamePool_IdleObjects);
    }
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 清空并回收指定纯 UObject 类别的所有空闲实例
 * @param ObjectClass 目标类
 */
void FUObjectPoolManager::ClearPool(TSubclassOf<UObject> ObjectClass) {
    UClass* RawClass = ObjectClass.Get();
    if (RawClass == nullptr) {
        return;
    }
    if (TPoolBucket<UObject>* Bucket = ObjectPoolMap.Find(RawClass)) {
        DestroyBucketIdleObjects(*Bucket);
    }
}

/**
 * @brief 清空并回收所有纯 UObject 类别的空闲实例
 */
void FUObjectPoolManager::ClearAllPools() {
    for (auto& Pair : ObjectPoolMap) {
        DestroyBucketIdleObjects(Pair.Value);
    }
}

/**
 * @brief 泄洪销毁所有类别的空闲 UObject 实例（从最冷端逐出，优先保留最热保底实例）
 * @param bPreserveMinCount 是否保留各池配置的 MinPreservedCount 保底数量
 * @return 实际销毁的实例数
 */
int32 FUObjectPoolManager::FlushIdleObjects(const bool bPreserveMinCount) {
    int32 TotalFlushed = 0;
    for (auto& Pair : ObjectPoolMap) {
        TPoolBucket<UObject>& Bucket = Pair.Value;
        const int32 PreservedLimit = bPreserveMinCount ? FMath::Max(0, Bucket.EvictionSettings.MinPreservedCount) : 0;

        while (Bucket.GetIdleCount() > PreservedLimit) {
            TPoolBucket<UObject>::FIdleEntry ColdEntry;
            Bucket.TryPopColdIdle(ColdEntry);
            DEC_DWORD_STAT(STAT_GamePool_IdleObjects);
            DiscardIdleObject(ColdEntry.Instance.Get());
            ++TotalFlushed;
        }
    }
    return TotalFlushed;
}

/**
 * @brief 查询指定 UObject 类别空闲池是否已达 MaxIdleCount 容量上限
 * @param ObjectClass 目标 UObject 类
 * @return 已达上限返回 true
 */
bool FUObjectPoolManager::IsAtIdleCapacity(TSubclassOf<UObject> ObjectClass) const {
    const UClass* RawClass = ObjectClass.Get();
    if (RawClass == nullptr) {
        return false;
    }
    const TPoolBucket<UObject>* Bucket = ObjectPoolMap.Find(RawClass);
    return Bucket != nullptr && Bucket->MaxIdleCount > 0 && Bucket->GetIdleCount() >= Bucket->MaxIdleCount;
}

/**
 * @brief 向 UE 垃圾回收系统注册 Outer、全部 Class Key、空闲与活跃 UObject 强引用
 * @param Collector 引用收集器
 * @param ReferencingObject 引用宿主对象
 */
void FUObjectPoolManager::AddReferencedObjects(FReferenceCollector& Collector, const UObject* ReferencingObject) {
    Collector.AddReferencedObject(OuterObject, ReferencingObject);
    for (auto& Pair : ObjectPoolMap) {
        Collector.AddReferencedObject(Pair.Key, ReferencingObject);
        Pair.Value.AddReferencedObjects(Collector, ReferencingObject);
    }
}

/**
 * @brief 获取全部类别的活跃 UObject 实例总数
 * @return 活跃 UObject 实例总数
 */
int32 FUObjectPoolManager::GetTotalActiveCount() const noexcept {
    int32 Sum = 0;
    for (const auto& Pair : ObjectPoolMap) {
        Sum += Pair.Value.GetActiveCount();
    }
    return Sum;
}

/**
 * @brief 获取全部类别的空闲 UObject 实例总数
 * @return 空闲 UObject 实例总数
 */
int32 FUObjectPoolManager::GetTotalIdleCount() const noexcept {
    int32 Sum = 0;
    for (const auto& Pair : ObjectPoolMap) {
        Sum += Pair.Value.GetIdleCount();
    }
    return Sum;
}

/**
 * @brief 重置遥测统计计数器
 */
void FUObjectPoolManager::ResetTelemetry() noexcept {
    TotalAcquires = 0;
    CacheHits = 0;
    CacheMisses = 0;
}

/**
 * @brief 标记回收单个空闲 UObject 并清理其世代记录
 * @param Object 待回收对象指针
 */
void FUObjectPoolManager::DiscardIdleObject(UObject* Object) {
    if (Object == nullptr) {
        return;
    }
    if (GenerationTracker != nullptr) {
        GenerationTracker->RemoveGeneration(Object);
    }
    if (IsValid(Object)) {
        Object->MarkAsGarbage();
    }
}

/**
 * @brief 标记回收指定存储桶中的全部空闲 UObject 并同步扣减空闲 Stat
 * @param Bucket 目标存储桶
 */
void FUObjectPoolManager::DestroyBucketIdleObjects(TPoolBucket<UObject>& Bucket) {
    const int32 IdleCount = Bucket.GetIdleCount();
    if (IdleCount <= 0) {
        return;
    }

    TPoolBucket<UObject>::FIdleEntry Entry;
    while (Bucket.TryPopColdIdle(Entry)) {
        DiscardIdleObject(Entry.Instance.Get());
    }
    DEC_DWORD_STAT_BY(STAT_GamePool_IdleObjects, IdleCount);
}
