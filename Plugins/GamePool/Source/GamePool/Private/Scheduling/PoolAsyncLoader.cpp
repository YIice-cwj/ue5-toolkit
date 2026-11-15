// Copyright Epic Games, Inc. All Rights Reserved.

#include "Scheduling/PoolAsyncLoader.h"
#include "Managers/ActorPoolManager.h"
#include "Managers/UObjectPoolManager.h"
#include "Telemetry/ObjectPoolStats.h"
#include "Async/FGameCoroAwaiters.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

/**
 * @brief 异步解析软引用路径，若未载入则通过 StreamableManager 流式加载
 * @param WorldContextObject 世界上下文对象
 * @param ClassPath 目标类软引用路径
 * @param CancellationToken 协程取消令牌
 * @return 成功加载的 UClass 指针，取消或加载失败返回 nullptr
 */
TGameCoroTask<UClass*> FPoolAsyncLoader::ResolveOrAsyncLoadClass(
    UObject* WorldContextObject,
    const FSoftObjectPath& ClassPath,
    FCancellationToken CancellationToken
) {
    co_await Game::Coro::BindContext(WorldContextObject, CancellationToken);
    if (!ClassPath.IsValid() || CancellationToken.IsCancellationRequested()) {
        co_return nullptr;
    }

    if (UClass* Loaded = Cast<UClass>(ClassPath.ResolveObject())) {
        co_return Loaded;
    }

    TSharedPtr<FStreamableHandle> LoadHandle;
    if (UAssetManager::IsInitialized()) {
        LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(ClassPath);
    } else {
        static FStreamableManager StaticStreamable;
        LoadHandle = StaticStreamable.RequestAsyncLoad(ClassPath);
    }

    if (LoadHandle.IsValid()) {
        while (!LoadHandle->HasLoadCompleted() && !CancellationToken.IsCancellationRequested()) {
            co_await Game::Coro::YieldTimeSlice(WorldContextObject);
        }
        if (CancellationToken.IsCancellationRequested()) {
            LoadHandle->CancelHandle();
            co_return nullptr;
        }
    }

    if (CancellationToken.IsCancellationRequested()) {
        co_return nullptr;
    }

    co_return Cast<UClass>(ClassPath.ResolveObject());
}

/**
 * @brief 异步分步预热指定 Actor 类别
 * @param WorldContextObject 世界上下文对象
 * @param ActorManager Actor 池管理器
 * @param ActorClass 目标 Actor 类
 * @param PrewarmCount 目标预热实例数量
 * @param MaxSpawnsPerStep 单步最大实例化配额（防单帧顿卡）
 * @param CancellationToken 协程取消令牌
 * @return 实际成功生成的空闲实例数量
 */
TGameCoroTask<int32> FPoolAsyncLoader::PrewarmActorsAsync(
    UObject* WorldContextObject,
    FActorPoolManager& ActorManager,
    const TSubclassOf<AActor> ActorClass,
    const int32 PrewarmCount,
    const int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken
) {
    co_await Game::Coro::BindContext(WorldContextObject, CancellationToken);
    if (!ActorClass || ActorClass->HasAnyClassFlags(CLASS_Abstract) || PrewarmCount <= 0 || CancellationToken.IsCancellationRequested()) {
        co_return 0;
    }

    const int32 BatchSize = FMath::Max(1, MaxSpawnsPerStep);
    int32 SpawnedCount = 0;

    for (int32 Index = 0; Index < PrewarmCount; ++Index) {
        if (CancellationToken.IsCancellationRequested()) {
            break;
        }

        const FPoolBucket* Bucket = ActorManager.FindBucket(ActorClass);
        if (Bucket != nullptr && Bucket->MaxIdleCount > 0 && Bucket->IdleActors.Num() >= Bucket->MaxIdleCount) {
            break;
        }

        bool bSpawnSuccess = false;
        {
            SCOPE_CYCLE_COUNTER(STAT_GamePool_PrewarmStep);
            TRACE_CPUPROFILER_EVENT_SCOPE(GamePool_PrewarmStep);
            bSpawnSuccess = ActorManager.SpawnIdleActorToPool(ActorClass);
        }
        if (!bSpawnSuccess) {
            break;
        }

        ++SpawnedCount;
        if ((SpawnedCount % BatchSize) == 0 && SpawnedCount < PrewarmCount) {
            co_await Game::Coro::YieldTimeSlice(WorldContextObject);
        }
    }

    co_return SpawnedCount;
}

/**
 * @brief 异步流式加载并分步预热 Actor 软引用资产
 * @param WorldContextObject 世界上下文对象
 * @param ActorManager Actor 池管理器
 * @param ActorClassPath 目标 Actor 类软引用路径
 * @param PrewarmCount 目标预热实例数量
 * @param MaxSpawnsPerStep 单步最大实例化配额
 * @param CancellationToken 协程取消令牌
 * @return 实际成功生成的空闲实例数量，若加载失败或取消返回 0
 */
TGameCoroTask<int32> FPoolAsyncLoader::PrewarmActorsByPathAsync(
    UObject* WorldContextObject,
    FActorPoolManager& ActorManager,
    const FSoftObjectPath& ActorClassPath,
    const int32 PrewarmCount,
    const int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken
) {
    co_await Game::Coro::BindContext(WorldContextObject, CancellationToken);
    if (!ActorClassPath.IsValid() || PrewarmCount <= 0 || CancellationToken.IsCancellationRequested()) {
        co_return 0;
    }

    const TGameStatusResult<UClass*> LoadResult = co_await ResolveOrAsyncLoadClass(WorldContextObject, ActorClassPath, CancellationToken);
    UClass* LoadedClass = LoadResult.IsSuccess() ? LoadResult.GetValue() : nullptr;
    if (LoadedClass == nullptr || !LoadedClass->IsChildOf(AActor::StaticClass()) || LoadedClass->HasAnyClassFlags(CLASS_Abstract) || CancellationToken.IsCancellationRequested()) {
        co_return 0;
    }

    const TGameStatusResult<int32> NestedResult = co_await PrewarmActorsAsync(
        WorldContextObject,
        ActorManager,
        TSubclassOf<AActor>(LoadedClass),
        PrewarmCount,
        MaxSpawnsPerStep,
        CancellationToken
    );
    co_return NestedResult.IsSuccess() ? NestedResult.GetValue() : 0;
}

/**
 * @brief 异步分步预热指定纯 UObject 类别
 * @param WorldContextObject 世界上下文对象
 * @param ObjectManager UObject 池管理器
 * @param ObjectClass 目标 UObject 类
 * @param PrewarmCount 目标预热实例数量
 * @param MaxSpawnsPerStep 单步最大实例化配额
 * @param CancellationToken 协程取消令牌
 * @return 实际成功生成的空闲实例数量
 */
TGameCoroTask<int32> FPoolAsyncLoader::PrewarmObjectsAsync(
    UObject* WorldContextObject,
    FUObjectPoolManager& ObjectManager,
    const TSubclassOf<UObject> ObjectClass,
    const int32 PrewarmCount,
    const int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken
) {
    co_await Game::Coro::BindContext(WorldContextObject, CancellationToken);
    if (!ObjectClass || ObjectClass->HasAnyClassFlags(CLASS_Abstract) || PrewarmCount <= 0 || CancellationToken.IsCancellationRequested()) {
        co_return 0;
    }

    const int32 BatchSize = FMath::Max(1, MaxSpawnsPerStep);
    int32 SpawnedCount = 0;

    for (int32 Index = 0; Index < PrewarmCount; ++Index) {
        if (CancellationToken.IsCancellationRequested()) {
            break;
        }

        const FObjectPoolBucket* Bucket = ObjectManager.FindBucket(ObjectClass);
        if (Bucket != nullptr && Bucket->MaxIdleCount > 0 && Bucket->IdleObjects.Num() >= Bucket->MaxIdleCount) {
            break;
        }

        bool bSpawnSuccess = false;
        {
            SCOPE_CYCLE_COUNTER(STAT_GamePool_PrewarmStep);
            TRACE_CPUPROFILER_EVENT_SCOPE(GamePool_PrewarmObjectStep);
            bSpawnSuccess = ObjectManager.SpawnIdleObjectToPool(ObjectClass);
        }
        if (!bSpawnSuccess) {
            break;
        }

        ++SpawnedCount;
        if ((SpawnedCount % BatchSize) == 0 && SpawnedCount < PrewarmCount) {
            co_await Game::Coro::YieldTimeSlice(WorldContextObject);
        }
    }

    co_return SpawnedCount;
}

/**
 * @brief 异步流式加载并分步预热纯 UObject 软引用资产
 * @param WorldContextObject 世界上下文对象
 * @param ObjectManager UObject 池管理器
 * @param ObjectClassPath 目标 UObject 类软引用路径
 * @param PrewarmCount 目标预热实例数量
 * @param MaxSpawnsPerStep 单步最大实例化配额
 * @param CancellationToken 协程取消令牌
 * @return 实际成功生成的空闲实例数量，若加载失败或取消返回 0
 */
TGameCoroTask<int32> FPoolAsyncLoader::PrewarmObjectsByPathAsync(
    UObject* WorldContextObject,
    FUObjectPoolManager& ObjectManager,
    const FSoftObjectPath& ObjectClassPath,
    const int32 PrewarmCount,
    const int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken
) {
    co_await Game::Coro::BindContext(WorldContextObject, CancellationToken);
    if (!ObjectClassPath.IsValid() || PrewarmCount <= 0 || CancellationToken.IsCancellationRequested()) {
        co_return 0;
    }

    const TGameStatusResult<UClass*> LoadResult = co_await ResolveOrAsyncLoadClass(WorldContextObject, ObjectClassPath, CancellationToken);
    UClass* LoadedClass = LoadResult.IsSuccess() ? LoadResult.GetValue() : nullptr;
    if (LoadedClass == nullptr || LoadedClass->IsChildOf(AActor::StaticClass()) || LoadedClass->HasAnyClassFlags(CLASS_Abstract) || CancellationToken.IsCancellationRequested()) {
        co_return 0;
    }

    const TGameStatusResult<int32> NestedResult = co_await PrewarmObjectsAsync(
        WorldContextObject,
        ObjectManager,
        TSubclassOf<UObject>(LoadedClass),
        PrewarmCount,
        MaxSpawnsPerStep,
        CancellationToken
    );
    co_return NestedResult.IsSuccess() ? NestedResult.GetValue() : 0;
}
