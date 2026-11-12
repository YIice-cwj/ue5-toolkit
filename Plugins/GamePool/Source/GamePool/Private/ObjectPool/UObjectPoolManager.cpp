// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectPool/UObjectPoolManager.h"
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
        Bucket.IdleObjectSet.Add(NewInstance);
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
            Bucket.IdleObjectSet.Remove(ObjToDiscard);
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
            Bucket.IdleObjectSet.Remove(Candidate);
        }
        if (IsValid(Candidate.Get())) {
            ResultObject = Candidate.Get();
            break;
        }
    }

    if (ResultObject == nullptr) {
        ResultObject = NewObject<UObject>(OuterObject, RawClass);
    }
    if (ResultObject == nullptr) {
        return TGameStatusResult<UObject*>::Failure(5002, TEXT("Failed to instantiate UObject"));
    }

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
        return TGameStatusResult<UObject*>::Failure(5002, TEXT("UObject was invalidated during OnAcquireFromPool"));
    }
    if (Bucket == nullptr || !Bucket->ActiveObjectSet.Contains(ResultObject)) {
        if (GenerationTracker != nullptr) {
            GenerationTracker->InvalidateGeneration(ResultObject);
        }
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
        CurrentBucket.IdleObjectSet.Add(Object);
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
