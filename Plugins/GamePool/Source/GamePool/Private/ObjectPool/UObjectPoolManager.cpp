// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectPool/UObjectPoolManager.h"
#include "ObjectPool/ObjectPoolStats.h"
#include "ObjectPool/PooledActorResetter.h"
#include "Config/GameCompileSwitches.h"
#include "GameFramework/Actor.h"

namespace {

DEFINE_LOG_CATEGORY_STATIC(LogGameUObjectPool, Log, All);

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
 * @brief 同步预热指定纯 UObject 类别
 * @param ObjectClass 目标 UObject 类
 * @param PrewarmCount 预热数量
 */
void FUObjectPoolManager::Prewarm(TSubclassOf<UObject> ObjectClass, int32 PrewarmCount) {
    UClass* RawClass = ObjectClass.Get();
    if (!IsValidPoolableUObjectClass(RawClass) || PrewarmCount <= 0 || OuterObject == nullptr) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameUObjectPool, Warning, TEXT("【UObject对象池】预热失败：ObjectClass 无效或预热数量 <= 0。"));
        }
        return;
    }

    FObjectPoolBucket& Bucket = ObjectPoolMap.FindOrAdd(RawClass);
    for (int32 Index = 0; Index < PrewarmCount; ++Index) {
        if (Bucket.MaxIdleCount > 0 && Bucket.IdleObjects.Num() >= Bucket.MaxIdleCount) {
            break;
        }

        UObject* NewInstance = NewObject<UObject>(OuterObject, RawClass);
        if (NewInstance == nullptr) {
            break;
        }
        Bucket.IdleObjects.Add(NewInstance);
        Bucket.IdleReturnTimestamps.Add(FPlatformTime::Seconds());
        Bucket.IdleObjectSet.Add(NewInstance);
        INC_DWORD_STAT(STAT_GamePool_IdleObjects);
    }

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameUObjectPool, Log, TEXT("【UObject对象池】预热完成: 类=[%s], 请求数量=%d, 当前空闲池大小=%d"),
            *RawClass->GetName(), PrewarmCount, Bucket.IdleObjects.Num());
    }
}

/**
 * @brief 设置指定 UObject 类的最大空闲缓存容量
 * @param ObjectClass 目标类
 * @param InMaxIdleCount 最大空闲数
 */
void FUObjectPoolManager::SetMaxCapacity(TSubclassOf<UObject> ObjectClass, int32 InMaxIdleCount) {
    UClass* RawClass = ObjectClass.Get();
    if (RawClass == nullptr) {
        return;
    }

    FObjectPoolBucket& Bucket = ObjectPoolMap.FindOrAdd(RawClass);
    Bucket.MaxIdleCount = InMaxIdleCount;
    if (InMaxIdleCount > 0 && Bucket.IdleObjects.Num() > InMaxIdleCount) {
        const int32 TrimCount = Bucket.IdleObjects.Num() - InMaxIdleCount;
        for (int32 Index = 0; Index < TrimCount; ++Index) {
            TObjectPtr<UObject> ObjToDiscard = Bucket.IdleObjects.Pop();
            if (Bucket.IdleReturnTimestamps.Num() > 0) {
                Bucket.IdleReturnTimestamps.Pop();
            }
            Bucket.IdleObjectSet.Remove(ObjToDiscard);
            DEC_DWORD_STAT(STAT_GamePool_IdleObjects);
            if (GenerationTracker != nullptr) {
                GenerationTracker->InvalidateGeneration(ObjToDiscard.Get());
            }
            if (IsValid(ObjToDiscard.Get())) {
                ObjToDiscard->MarkAsGarbage();
            }
        }
    }
}

/**
 * @brief 配置指定纯 UObject 类别的闲置弹性缩容与淘汰策略
 * @param ObjectClass 目标类
 * @param InSettings 淘汰策略配置
 */
void FUObjectPoolManager::SetPoolEvictionSettings(TSubclassOf<UObject> ObjectClass, const FPoolEvictionSettings& InSettings) {
    UClass* RawClass = ObjectClass.Get();
    if (RawClass == nullptr) {
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
    if (RawClass != nullptr) {
        if (const FObjectPoolBucket* Bucket = ObjectPoolMap.Find(RawClass)) {
            return Bucket->EvictionSettings;
        }
    }
    return FPoolEvictionSettings();
}

/**
 * @brief 执行超时冷 UObject 实例淘汰
 * @param NowSeconds 当前单调硬件时间戳（秒）
 * @param MaxEvictCount 单次最大销毁配额（防止单步 GC 顿卡）
 * @return 实际销毁的冷实例总数
 */
int32 FUObjectPoolManager::EvictExpiredObjects(const double NowSeconds, const int32 MaxEvictCount) {
    SCOPE_CYCLE_COUNTER(STAT_GamePool_EvictStep);
    TRACE_CPUPROFILER_EVENT_SCOPE(GamePool_EvictObjects);

    if (MaxEvictCount <= 0) {
        return 0;
    }

    int32 TotalEvicted = 0;
    for (auto& Pair : ObjectPoolMap) {
        FObjectPoolBucket& Bucket = Pair.Value;
        if (!Bucket.EvictionSettings.bEnableEviction) {
            continue;
        }

        const float Ttl = Bucket.EvictionSettings.IdleTimeToLiveSeconds;
        const int32 MinPreserved = FMath::Max(0, Bucket.EvictionSettings.MinPreservedCount);

        while (TotalEvicted < MaxEvictCount &&
               Bucket.IdleObjects.Num() > MinPreserved &&
               Bucket.IdleReturnTimestamps.Num() > 0) {
            const double IdleTime = NowSeconds - Bucket.IdleReturnTimestamps[0];
            if (IdleTime < Ttl) {
                break;
            }

            TObjectPtr<UObject> ObjToDiscard = Bucket.IdleObjects[0];
            Bucket.IdleObjects.RemoveAt(0);
            Bucket.IdleReturnTimestamps.RemoveAt(0);
            Bucket.IdleObjectSet.Remove(ObjToDiscard);
            DEC_DWORD_STAT(STAT_GamePool_IdleObjects);

            if (UObject* RawObj = ObjToDiscard.Get()) {
                if (GenerationTracker != nullptr) {
                    GenerationTracker->InvalidateGeneration(RawObj);
                }
                if (IsValid(RawObj)) {
                    RawObj->MarkAsGarbage();
                }
            }
            ++TotalEvicted;
        }

        if (TotalEvicted >= MaxEvictCount) {
            break;
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

    if (const FObjectPoolBucket* Bucket = ObjectPoolMap.Find(RawClass)) {
        Stats.IdleCount = Bucket->IdleObjects.Num();
        Stats.ActiveCount = Bucket->ActiveCount;
        Stats.TotalCount = Stats.IdleCount + Stats.ActiveCount;
    }
    return Stats;
}

/**
 * @brief 从池中借出或构建纯 UObject 实例
 * @param ObjectClass 目标类
 * @return 成功返回 UObject 指针，失败返回 Failure
 */
TGameStatusResult<UObject*> FUObjectPoolManager::AcquireObject(TSubclassOf<UObject> ObjectClass) {
    SCOPE_CYCLE_COUNTER(STAT_GamePool_AcquireObject);
    TRACE_CPUPROFILER_EVENT_SCOPE(GamePool_AcquireObject);

    UClass* RawClass = ObjectClass.Get();
    if (!IsValidPoolableUObjectClass(RawClass) || OuterObject == nullptr) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameUObjectPool, Warning, TEXT("【UObject对象池】借出 UObject 失败：ObjectClass 无效或属于 AActor 派生类。"));
        }
        return TGameStatusResult<UObject*>::Failure(5001, TEXT("ObjectClass is invalid or is AActor subclass"));
    }

    UObject* ResultObject = nullptr;
    while (true) {
        TObjectPtr<UObject> Candidate;
        {
            FObjectPoolBucket& Bucket = ObjectPoolMap.FindOrAdd(RawClass);
            if (Bucket.IdleObjects.IsEmpty()) {
                break;
            }
            Candidate = Bucket.IdleObjects.Pop();
            if (Bucket.IdleReturnTimestamps.Num() > 0) {
                Bucket.IdleReturnTimestamps.Pop();
            }
            Bucket.IdleObjectSet.Remove(Candidate);
        }
        if (IsValid(Candidate.Get())) {
            ResultObject = Candidate.Get();
            break;
        }
    }

    const bool bReused = (ResultObject != nullptr);
    if (bReused) {
        ++CacheHits;
        INC_DWORD_STAT(STAT_GamePool_Hits);
        DEC_DWORD_STAT(STAT_GamePool_IdleObjects);
    } else {
        ++CacheMisses;
        INC_DWORD_STAT(STAT_GamePool_Misses);
    }
    ++TotalAcquires;

    if (ResultObject == nullptr) {
        ResultObject = NewObject<UObject>(OuterObject, RawClass);
    }
    if (ResultObject == nullptr) {
        return TGameStatusResult<UObject*>::Failure(5002, TEXT("Failed to instantiate UObject"));
    }

    INC_DWORD_STAT(STAT_GamePool_ActiveObjects);

    {
        FObjectPoolBucket& Bucket = ObjectPoolMap.FindOrAdd(RawClass);
        Bucket.ActiveObjectSet.Add(ResultObject);
        ++Bucket.ActiveCount;
    }

    if (GenerationTracker != nullptr) {
        GenerationTracker->GetOrIncrementGeneration(ResultObject);
    }

    FPooledActorResetter::InvokePoolableCallback(ResultObject, true);
    FObjectPoolBucket* Bucket = ObjectPoolMap.Find(RawClass);
    if (!IsValid(ResultObject)) {
        if (GenerationTracker != nullptr) {
            GenerationTracker->InvalidateGeneration(ResultObject);
        }
        if (Bucket != nullptr && Bucket->ActiveObjectSet.Remove(ResultObject) > 0) {
            --Bucket->ActiveCount;
        }
        DEC_DWORD_STAT(STAT_GamePool_ActiveObjects);
        return TGameStatusResult<UObject*>::Failure(5002, TEXT("UObject was invalidated during OnAcquireFromPool"));
    }
    if (Bucket == nullptr || !Bucket->ActiveObjectSet.Contains(ResultObject)) {
        if (GenerationTracker != nullptr) {
            GenerationTracker->InvalidateGeneration(ResultObject);
        }
        DEC_DWORD_STAT(STAT_GamePool_ActiveObjects);
        return TGameStatusResult<UObject*>::Failure(5002, TEXT("UObject was invalidated during OnAcquireFromPool"));
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
        return TGameStatusResult<void>::Failure(5004, TEXT("Object is invalid or already destroyed"));
    }

    UClass* RawClass = Object->GetClass();
    FObjectPoolBucket* Bucket = ObjectPoolMap.Find(RawClass);
    if (Bucket == nullptr || Bucket->ActiveObjectSet.Remove(Object) == 0) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameUObjectPool, Warning, TEXT("【UObject对象池】归还警告：UObject [%s] 不是当前活跃池属实例，已忽略归还。"),
                *GetNameSafe(Object));
        }
        return TGameStatusResult<void>::Failure(5003, TEXT("Object is not an active member of this pool"));
    }
    --Bucket->ActiveCount;
    DEC_DWORD_STAT(STAT_GamePool_ActiveObjects);

    if (GenerationTracker != nullptr) {
        GenerationTracker->InvalidateGeneration(Object);
    }

    FPooledActorResetter::InvokePoolableCallback(Object, false);
    if (!IsValid(Object)) {
        return TGameStatusResult<void>::Success();
    }

    FObjectPoolBucket& CurrentBucket = ObjectPoolMap.FindOrAdd(RawClass);
    if (CurrentBucket.MaxIdleCount > 0 && CurrentBucket.IdleObjects.Num() >= CurrentBucket.MaxIdleCount) {
        Object->MarkAsGarbage();
    } else {
        CurrentBucket.IdleObjects.Add(Object);
        CurrentBucket.IdleReturnTimestamps.Add(FPlatformTime::Seconds());
        CurrentBucket.IdleObjectSet.Add(Object);
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
    if (FObjectPoolBucket* Bucket = ObjectPoolMap.Find(RawClass)) {
        DestroyBucketIdleObjects(*Bucket);
    }
}

/**
 * @brief 清空并回收所有纯 UObject 类别的空闲实例
 */
void FUObjectPoolManager::ClearAllPools() {
    TArray<TObjectPtr<UClass>> ObjectClasses;
    ObjectPoolMap.GenerateKeyArray(ObjectClasses);
    for (const TObjectPtr<UClass>& ObjectClass : ObjectClasses) {
        if (FObjectPoolBucket* Bucket = ObjectPoolMap.Find(ObjectClass)) {
            DestroyBucketIdleObjects(*Bucket);
        }
    }
}

/**
 * @brief 销毁并标记回收指定存储桶中的全部空闲 UObject
 * @param Bucket 目标存储桶
 */
void FUObjectPoolManager::DestroyBucketIdleObjects(FObjectPoolBucket& Bucket) {
    TArray<TObjectPtr<UObject>> IdleObjects = MoveTemp(Bucket.IdleObjects);
    Bucket.IdleObjects.Empty();
    Bucket.IdleReturnTimestamps.Empty();
    Bucket.IdleObjectSet.Empty();
    for (TObjectPtr<UObject>& IdleObj : IdleObjects) {
        if (IsValid(IdleObj.Get())) {
            if (GenerationTracker != nullptr) {
                GenerationTracker->InvalidateGeneration(IdleObj.Get());
            }
            IdleObj->MarkAsGarbage();
        }
    }
}

/**
 * @brief FGCObject 引用收集回调
 * @param Collector 引用收集器
 */
void FUObjectPoolManager::AddReferencedObjects(FReferenceCollector& Collector) {
    for (auto& Pair : ObjectPoolMap) {
        Collector.AddReferencedObjects(Pair.Value.IdleObjects);
        Collector.AddReferencedObjects(Pair.Value.IdleObjectSet);
        Collector.AddReferencedObjects(Pair.Value.ActiveObjectSet);
    }
}

/**
 * @brief 获取全部类别的活跃 UObject 实例总数
 * @return 活跃 UObject 实例总数
 */
int32 FUObjectPoolManager::GetTotalActiveCount() const noexcept {
    int32 Sum = 0;
    for (const auto& Pair : ObjectPoolMap) {
        Sum += Pair.Value.ActiveCount;
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
        Sum += Pair.Value.IdleObjects.Num();
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
