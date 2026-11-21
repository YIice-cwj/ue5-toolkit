/**
 * @file FPoolAsyncLoader.cpp
 * @brief 事件驱动异步资产流式加载与 C++20 协程分步预热内部调度器实现
 */

#include "Scheduling/FPoolAsyncLoader.h"

#include "Async/FGameCoroAwaiters.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Actor.h"
#include "Managers/FActorPoolManager.h"
#include "Managers/FUObjectPoolManager.h"
#include "Telemetry/ObjectPoolStats.h"

#include <atomic>

namespace {

/**
 * @brief 基于 FStreamableManager 完成委托的事件驱动类异步加载 Awaiter（共享状态隔离同步回调析构与取消竞态）
 */
class FStreamableClassLoadAwaiter {
private:
    struct FSharedLoadState {
        TSharedPtr<FStreamableHandle> Handle;
        FCancellationTokenRegistration CancelRegistration;
        std::atomic<bool> bFinished{false};
    };

public:
    FStreamableClassLoadAwaiter(FSoftObjectPath InTargetPath, FCancellationToken InToken)
        : TargetPath(MoveTemp(InTargetPath))
        , Token(MoveTemp(InToken))
        , LoadState(MakeShared<FSharedLoadState, ESPMode::ThreadSafe>()) {
    }

    [[nodiscard]] bool await_ready() {
        if (!TargetPath.IsValid()) {
            return true;
        }
        ResolvedClass = Cast<UClass>(TargetPath.ResolveObject());
        return ResolvedClass != nullptr;
    }

    void SuspendWithGuard(TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard) {
        if (!UAssetManager::IsInitialized()) {
            InGuard->ResumeOrAbort();
            return;
        }

        const TSharedRef<FSharedLoadState, ESPMode::ThreadSafe> LocalState = LoadState;
        const FSoftObjectPath LocalPath = TargetPath;
        FCancellationToken LocalToken = Token;

        FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
        TSharedPtr<FStreamableHandle> LocalHandle = Streamable.RequestAsyncLoad(
            LocalPath,
            FStreamableDelegate::CreateLambda([InGuard, LocalState]() {
                if (LocalState->bFinished.exchange(true, std::memory_order_acq_rel)) {
                    return;
                }
                LocalState->CancelRegistration.Unregister();
                InGuard->ResumeOrAbort();
            })
        );

        if (LocalState->bFinished.load(std::memory_order_acquire)) {
            return;
        }

        LocalState->Handle = LocalHandle;
        if (!LocalHandle.IsValid()) {
            if (!LocalState->bFinished.exchange(true, std::memory_order_acq_rel)) {
                InGuard->ResumeOrAbort();
            }
            return;
        }

        if (!LocalToken.CanBeCancelled()) {
            return;
        }
        TWeakPtr<FStreamableHandle> WeakHandle = LocalHandle;
        LocalState->CancelRegistration = LocalToken.Register([WeakHandle, InGuard, LocalState]() {
            if (LocalState->bFinished.exchange(true, std::memory_order_acq_rel)) {
                return;
            }
            if (TSharedPtr<FStreamableHandle> Pinned = WeakHandle.Pin()) {
                Pinned->CancelHandle();
            }
            InGuard->AbortAndDestroyFrame();
        });
    }

    [[nodiscard]] UClass* await_resume() const {
        if (ResolvedClass != nullptr) {
            return ResolvedClass;
        }
        if (LoadState->Handle.IsValid() && LoadState->Handle->HasLoadCompleted()) {
            if (UClass* Loaded = Cast<UClass>(LoadState->Handle->GetLoadedAsset())) {
                return Loaded;
            }
        }
        return Cast<UClass>(TargetPath.ResolveObject());
    }

private:
    FSoftObjectPath TargetPath;
    FCancellationToken Token;
    UClass* ResolvedClass = nullptr;
    TSharedRef<FSharedLoadState, ESPMode::ThreadSafe> LoadState;
};

template <typename TValidateClassFn, typename TIsAtCapacityFn, typename TSpawnOneFn>
TGameCoroTask<int32> ExecutePrewarmCoro(
    UObject* WorldContextObject,
    TWeakPtr<const void> ImplGuard,
    UClass* PreResolvedClass,
    FSoftObjectPath ClassPath,
    const int32 PrewarmCount,
    const int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken,
    const bool bIsActorPool,
    TValidateClassFn ValidateClass,
    TIsAtCapacityFn IsAtCapacity,
    TSpawnOneFn SpawnOne
) {
    co_await Game::Coro::BindContext(WorldContextObject, CancellationToken);
    if (PrewarmCount <= 0 || !ImplGuard.IsValid()) {
        co_return 0;
    }

    UClass* TargetClass = PreResolvedClass;
    if (TargetClass == nullptr) {
        if (!ClassPath.IsValid()) {
            co_return 0;
        }
        TargetClass = co_await FStreamableClassLoadAwaiter(MoveTemp(ClassPath), CancellationToken);
    }

    if (!ImplGuard.IsValid() || !ValidateClass(TargetClass)) {
        co_return 0;
    }

    const int32 BatchSize = FMath::Max(1, MaxSpawnsPerStep);
    int32 SpawnedCount = 0;

    for (int32 Index = 0; Index < PrewarmCount; ++Index) {
        if (CancellationToken.IsCancellationRequested()) {
            co_await Game::Coro::YieldTimeSlice(WorldContextObject);
        }
        if (!ImplGuard.IsValid() || IsAtCapacity(TargetClass)) {
            break;
        }

        bool bSpawnSuccess = false;
        {
            SCOPE_CYCLE_COUNTER(STAT_GamePool_PrewarmStep);
            if (bIsActorPool) {
                TRACE_CPUPROFILER_EVENT_SCOPE(GamePool_PrewarmStep);
                bSpawnSuccess = SpawnOne(TargetClass);
            } else {
                TRACE_CPUPROFILER_EVENT_SCOPE(GamePool_PrewarmObjectStep);
                bSpawnSuccess = SpawnOne(TargetClass);
            }
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

bool IsValidActorClass(const UClass* TargetClass) {
    return TargetClass != nullptr &&
           TargetClass->IsChildOf(AActor::StaticClass()) &&
           !TargetClass->HasAnyClassFlags(CLASS_Abstract);
}

bool IsValidPureUObjectClass(const UClass* TargetClass) {
    return TargetClass != nullptr &&
           !TargetClass->IsChildOf(AActor::StaticClass()) &&
           !TargetClass->HasAnyClassFlags(CLASS_Abstract);
}

}  // namespace

/**
 * @brief 异步分步预热指定 Actor 类别
 * @param WorldContextObject 世界上下文或子系统宿主对象
 * @param ImplGuard 所属子系统 Pimpl 存活弱引用守卫
 * @param ActorManager Actor 池管理器引用
 * @param ActorClass 目标 Actor 类
 * @param PrewarmCount 目标预热实例数量
 * @param MaxSpawnsPerStep 单步最大实例化配额
 * @param CancellationToken 协作式取消令牌
 * @return 实际成功生成的空闲实例数量异步协程任务
 */
TGameCoroTask<int32> FPoolAsyncLoader::PrewarmActorsAsync(
    UObject* WorldContextObject,
    TWeakPtr<const void> ImplGuard,
    FActorPoolManager& ActorManager,
    const TSubclassOf<AActor> ActorClass,
    const int32 PrewarmCount,
    const int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken
) {
    return ExecutePrewarmCoro(
        WorldContextObject,
        MoveTemp(ImplGuard),
        ActorClass.Get(),
        FSoftObjectPath(),
        PrewarmCount,
        MaxSpawnsPerStep,
        MoveTemp(CancellationToken),
        true,
        &IsValidActorClass,
        [&ActorManager](UClass* TargetClass) {
            return ActorManager.IsAtIdleCapacity(TSubclassOf<AActor>(TargetClass));
        },
        [&ActorManager](UClass* TargetClass) {
            return ActorManager.SpawnIdleActorToPool(TSubclassOf<AActor>(TargetClass)).IsSuccess();
        }
    );
}

/**
 * @brief 事件驱动异步流式加载并分步预热 Actor 软引用资产
 * @param WorldContextObject 世界上下文或子系统宿主对象
 * @param ImplGuard 所属子系统 Pimpl 存活弱引用守卫
 * @param ActorManager Actor 池管理器引用
 * @param ActorClassPath 目标 Actor 类软引用路径
 * @param PrewarmCount 目标预热实例数量
 * @param MaxSpawnsPerStep 单步最大实例化配额
 * @param CancellationToken 协作式取消令牌
 * @return 实际成功生成的空闲实例数量异步协程任务
 */
TGameCoroTask<int32> FPoolAsyncLoader::PrewarmActorsByPathAsync(
    UObject* WorldContextObject,
    TWeakPtr<const void> ImplGuard,
    FActorPoolManager& ActorManager,
    const FSoftObjectPath& ActorClassPath,
    const int32 PrewarmCount,
    const int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken
) {
    return ExecutePrewarmCoro(
        WorldContextObject,
        MoveTemp(ImplGuard),
        nullptr,
        ActorClassPath,
        PrewarmCount,
        MaxSpawnsPerStep,
        MoveTemp(CancellationToken),
        true,
        &IsValidActorClass,
        [&ActorManager](UClass* TargetClass) {
            return ActorManager.IsAtIdleCapacity(TSubclassOf<AActor>(TargetClass));
        },
        [&ActorManager](UClass* TargetClass) {
            return ActorManager.SpawnIdleActorToPool(TSubclassOf<AActor>(TargetClass)).IsSuccess();
        }
    );
}

/**
 * @brief 异步分步预热指定纯 UObject 类别
 * @param WorldContextObject 世界上下文或子系统宿主对象
 * @param ImplGuard 所属子系统 Pimpl 存活弱引用守卫
 * @param ObjectManager UObject 池管理器引用
 * @param ObjectClass 目标 UObject 类
 * @param PrewarmCount 目标预热实例数量
 * @param MaxSpawnsPerStep 单步最大实例化配额
 * @param CancellationToken 协作式取消令牌
 * @return 实际成功生成的空闲实例数量异步协程任务
 */
TGameCoroTask<int32> FPoolAsyncLoader::PrewarmObjectsAsync(
    UObject* WorldContextObject,
    TWeakPtr<const void> ImplGuard,
    FUObjectPoolManager& ObjectManager,
    const TSubclassOf<UObject> ObjectClass,
    const int32 PrewarmCount,
    const int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken
) {
    return ExecutePrewarmCoro(
        WorldContextObject,
        MoveTemp(ImplGuard),
        ObjectClass.Get(),
        FSoftObjectPath(),
        PrewarmCount,
        MaxSpawnsPerStep,
        MoveTemp(CancellationToken),
        false,
        &IsValidPureUObjectClass,
        [&ObjectManager](UClass* TargetClass) {
            return ObjectManager.IsAtIdleCapacity(TSubclassOf<UObject>(TargetClass));
        },
        [&ObjectManager](UClass* TargetClass) {
            return ObjectManager.SpawnIdleObjectToPool(TSubclassOf<UObject>(TargetClass)).IsSuccess();
        }
    );
}

/**
 * @brief 事件驱动异步流式加载并分步预热纯 UObject 软引用资产
 * @param WorldContextObject 世界上下文或子系统宿主对象
 * @param ImplGuard 所属子系统 Pimpl 存活弱引用守卫
 * @param ObjectManager UObject 池管理器引用
 * @param ObjectClassPath 目标 UObject 类软引用路径
 * @param PrewarmCount 目标预热实例数量
 * @param MaxSpawnsPerStep 单步最大实例化配额
 * @param CancellationToken 协作式取消令牌
 * @return 实际成功生成的空闲实例数量异步协程任务
 */
TGameCoroTask<int32> FPoolAsyncLoader::PrewarmObjectsByPathAsync(
    UObject* WorldContextObject,
    TWeakPtr<const void> ImplGuard,
    FUObjectPoolManager& ObjectManager,
    const FSoftObjectPath& ObjectClassPath,
    const int32 PrewarmCount,
    const int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken
) {
    return ExecutePrewarmCoro(
        WorldContextObject,
        MoveTemp(ImplGuard),
        nullptr,
        ObjectClassPath,
        PrewarmCount,
        MaxSpawnsPerStep,
        MoveTemp(CancellationToken),
        false,
        &IsValidPureUObjectClass,
        [&ObjectManager](UClass* TargetClass) {
            return ObjectManager.IsAtIdleCapacity(TSubclassOf<UObject>(TargetClass));
        },
        [&ObjectManager](UClass* TargetClass) {
            return ObjectManager.SpawnIdleObjectToPool(TSubclassOf<UObject>(TargetClass)).IsSuccess();
        }
    );
}
