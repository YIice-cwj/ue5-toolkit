/**
 * @file FGameAssetLoader.h
 * @brief 弱引用守卫安全异步软引用资产加载器与协程加载原语
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "UObject/Class.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"

#include "Asset/FGameAssetLoadHandle.h"
#include "Asset/GameAssetTypes.h"
#include "Async/FCancellationToken.h"
#include "Async/FGameCoroAwaiters.h"
#include "Async/TGameCoroTask.h"
#include "Types/TGameResult.h"

/**
 * @brief 宿主弱引用自动防悬垂的软引用资产异步加载纯静态工具类
 */
class GAMEASSETCORE_API FGameAssetLoader {
public:
    FGameAssetLoader() = delete;

    /**
     * @brief 异步加载单个软引用资产（已驻留内存时同步短路回调，宿主失效时静默丢弃回调）
     * @tparam TAsset 目标 UObject 派生资产类型
     * @param SoftAsset 目标资产软引用
     * @param Owner 宿主对象弱引用（入队前或完成时若失效则自动短路不回调）
     * @param OnLoaded 加载结果回调（解析成功传入资产指针，路径不存在或类型不匹配传入 nullptr）
     * @param CancellationToken 协同取消令牌（默认 None）
     * @param Priority 业务语义优先级（默认 Normal）
     * @return 可取消的加载控制句柄（同步短路时返回空句柄）
     */
    template <typename TAsset>
    static FGameAssetLoadHandle LoadAsync(
        const TSoftObjectPtr<TAsset>& SoftAsset,
        TWeakObjectPtr<UObject> Owner,
        TFunction<void(TAsset*)> OnLoaded,
        const FCancellationToken& CancellationToken = FCancellationToken::None(),
        EGameAssetPriority Priority = EGameAssetPriority::Normal
    );

    /**
     * @brief 异步加载软引用类（已驻留内存时同步短路回调，宿主失效时静默丢弃回调）
     * @tparam TClass 目标类基类类型
     * @param SoftClass 目标类软引用
     * @param Owner 宿主对象弱引用（入队前或完成时若失效则自动短路不回调）
     * @param OnLoaded 加载结果回调（解析成功传入 UClass 指针，路径不存在或类型不匹配传入 nullptr）
     * @param CancellationToken 协同取消令牌（默认 None）
     * @param Priority 业务语义优先级（默认 Normal）
     * @return 可取消的加载控制句柄（同步短路时返回空句柄）
     */
    template <typename TClass>
    static FGameAssetLoadHandle LoadClassAsync(
        const TSoftClassPtr<TClass>& SoftClass,
        TWeakObjectPtr<UObject> Owner,
        TFunction<void(UClass*)> OnLoaded,
        const FCancellationToken& CancellationToken = FCancellationToken::None(),
        EGameAssetPriority Priority = EGameAssetPriority::Normal
    );

    /**
     * @brief 基于 C++20 协程的软引用资产异步加载任务
     * @tparam TAsset 目标 UObject 派生资产类型
     * @param SoftAsset 目标资产软引用
     * @param WorldContextObject 世界上下文或宿主对象
     * @param CancellationToken 协程取消令牌（默认 None）
     * @param Priority 业务语义优先级（默认 Normal）
     * @return 协程异步任务，返回加载成功的资产指针，取消或失败返回 nullptr
     */
    template <typename TAsset>
    static TGameCoroTask<TAsset*> LoadAsyncCoro(
        const TSoftObjectPtr<TAsset>& SoftAsset,
        UObject* WorldContextObject,
        FCancellationToken CancellationToken = FCancellationToken::None(),
        EGameAssetPriority Priority = EGameAssetPriority::Normal
    );

    /**
     * @brief 基于 C++20 协程的软引用类异步加载任务
     * @tparam TClass 目标类基类类型
     * @param SoftClass 目标类软引用
     * @param WorldContextObject 世界上下文或宿主对象
     * @param CancellationToken 协程取消令牌（默认 None）
     * @param Priority 业务语义优先级（默认 Normal）
     * @return 协程异步任务，返回加载成功的 UClass 指针，取消或失败返回 nullptr
     */
    template <typename TClass>
    static TGameCoroTask<UClass*> LoadClassAsyncCoro(
        const TSoftClassPtr<TClass>& SoftClass,
        UObject* WorldContextObject,
        FCancellationToken CancellationToken = FCancellationToken::None(),
        EGameAssetPriority Priority = EGameAssetPriority::Normal
    );

    /**
     * @brief 基于 C++20 协程的软引用资产批量异步加载任务（并发等待一组资产全部完成）
     * @tparam TAsset 目标 UObject 派生资产类型（默认 UObject）
     * @param SoftAssets 待加载的软引用资产数组
     * @param WorldContextObject 世界上下文或宿主对象
     * @param CancellationToken 协程取消令牌（默认 None）
     * @param Priority 业务语义优先级（默认 Normal）
     * @return 协程异步任务，返回与输入等长顺序对齐的已加载资产指针数组；若取消或加载失败则对应项为 nullptr
     */
    template <typename TAsset = UObject>
    static TGameCoroTask<TArray<TAsset*>> LoadBatchAsyncCoro(
        const TArray<TSoftObjectPtr<TAsset>>& SoftAssets,
        UObject* WorldContextObject,
        FCancellationToken CancellationToken = FCancellationToken::None(),
        EGameAssetPriority Priority = EGameAssetPriority::Normal
    );

    /**
     * @brief 异步批量加载一组软对象路径并在全部完成且宿主仍有效时触发通知（支持进度汇报与取消联动）
     * @param AssetPaths 待加载软对象路径数组（自动过滤空路径与已驻留项）
     * @param Owner 宿主对象弱引用
     * @param OnAllLoaded 批次完成回调
     * @param OnProgress 批次进度更新回调（0.0f ~ 1.0f 浮点比例，默认空）
     * @param CancellationToken 协同取消令牌（默认 None）
     * @param Priority 业务语义优先级（默认 Normal）
     * @return 可取消的加载控制句柄
     */
    static FGameAssetLoadHandle LoadBatchAsync(
        const TArray<FSoftObjectPath>& AssetPaths,
        TWeakObjectPtr<UObject> Owner,
        TFunction<void()> OnAllLoaded,
        TFunction<void(float Progress)> OnProgress = nullptr,
        const FCancellationToken& CancellationToken = FCancellationToken::None(),
        EGameAssetPriority Priority = EGameAssetPriority::Normal
    );

    /**
     * @brief 异步批量加载一组强类型软引用资产并在全部完成且宿主仍有效时触发通知
     * @tparam TAsset 目标 UObject 派生资产类型（默认 UObject）
     * @param SoftAssets 待加载软引用资产数组
     * @param Owner 宿主对象弱引用
     * @param OnAllLoaded 批次完成回调
     * @param OnProgress 批次进度更新回调（0.0f ~ 1.0f 浮点比例，默认空）
     * @param CancellationToken 协同取消令牌（默认 None）
     * @param Priority 业务语义优先级（默认 Normal）
     * @return 可取消的加载控制句柄
     */
    template <typename TAsset = UObject>
    static FGameAssetLoadHandle LoadBatchAsync(
        const TArray<TSoftObjectPtr<TAsset>>& SoftAssets,
        TWeakObjectPtr<UObject> Owner,
        TFunction<void()> OnAllLoaded,
        TFunction<void(float Progress)> OnProgress = nullptr,
        const FCancellationToken& CancellationToken = FCancellationToken::None(),
        EGameAssetPriority Priority = EGameAssetPriority::Normal
    );

    /**
     * @brief 同步阻塞加载单个软引用资产（若已驻留内存则直接返回）
     * @tparam TAsset 目标 UObject 派生资产类型
     * @param SoftAsset 目标资产软引用
     * @return 成功返回包含资产有效裸指针的强类型结果，失败返回对应的 FGameError
     */
    template <typename TAsset>
    static TGameStatusResult<TAsset*> LoadSync(const TSoftObjectPtr<TAsset>& SoftAsset);

    /**
     * @brief 同步阻塞加载软引用类（若已驻留内存则直接返回）
     * @tparam TClass 目标类基类类型
     * @param SoftClass 目标类软引用
     * @return 成功返回包含 UClass 指针的强类型结果，失败返回对应的 FGameError
     */
    template <typename TClass>
    static TGameStatusResult<UClass*> LoadClassSync(const TSoftClassPtr<TClass>& SoftClass);
};

template <typename TAsset>
FGameAssetLoadHandle FGameAssetLoader::LoadAsync(
    const TSoftObjectPtr<TAsset>& SoftAsset,
    TWeakObjectPtr<UObject> Owner,
    TFunction<void(TAsset*)> OnLoaded,
    const FCancellationToken& CancellationToken,
    EGameAssetPriority Priority
) {
    if (!Owner.IsValid() || !OnLoaded || SoftAsset.IsNull() || CancellationToken.IsCancellationRequested()) {
        return FGameAssetLoadHandle();
    }

    if (TAsset* ResidentAsset = SoftAsset.Get()) {
        OnLoaded(ResidentAsset);
        return FGameAssetLoadHandle();
    }

    struct FAsyncRequestContext {
        FCancellationTokenRegistration TokenRegistration;
    };
    TSharedRef<FAsyncRequestContext> RequestContext = MakeShared<FAsyncRequestContext>();

    const FSoftObjectPath TargetPath = SoftAsset.ToSoftObjectPath();
    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestAsyncLoad(
        TargetPath,
        FStreamableDelegate::CreateLambda([Owner, TargetPath, Callback = MoveTemp(OnLoaded), CancellationToken, RequestContext]() mutable {
            if (!Owner.IsValid() || CancellationToken.IsCancellationRequested()) {
                return;
            }
            Callback(Cast<TAsset>(TargetPath.ResolveObject()));
        }),
        ToAsyncLoadPriority(Priority)
    );

    if (RawHandle.IsValid() && CancellationToken.CanBeCancelled()) {
        TWeakPtr<FStreamableHandle> WeakHandle = RawHandle;
        RequestContext->TokenRegistration = CancellationToken.Register([WeakHandle]() {
            if (TSharedPtr<FStreamableHandle> Pinned = WeakHandle.Pin()) {
                Pinned->CancelHandle();
            }
        });
    }

    return FGameAssetLoadHandle(MoveTemp(RawHandle));
}

template <typename TClass>
FGameAssetLoadHandle FGameAssetLoader::LoadClassAsync(
    const TSoftClassPtr<TClass>& SoftClass,
    TWeakObjectPtr<UObject> Owner,
    TFunction<void(UClass*)> OnLoaded,
    const FCancellationToken& CancellationToken,
    EGameAssetPriority Priority
) {
    if (!Owner.IsValid() || !OnLoaded || SoftClass.IsNull() || CancellationToken.IsCancellationRequested()) {
        return FGameAssetLoadHandle();
    }

    if (UClass* ResidentClass = SoftClass.Get()) {
        OnLoaded(ResidentClass);
        return FGameAssetLoadHandle();
    }

    struct FClassAsyncRequestContext {
        FCancellationTokenRegistration TokenRegistration;
    };
    TSharedRef<FClassAsyncRequestContext> RequestContext = MakeShared<FClassAsyncRequestContext>();

    const FSoftObjectPath TargetPath = SoftClass.ToSoftObjectPath();
    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestAsyncLoad(
        TargetPath,
        FStreamableDelegate::CreateLambda([Owner, TargetPath, Callback = MoveTemp(OnLoaded), CancellationToken, RequestContext]() mutable {
            if (!Owner.IsValid() || CancellationToken.IsCancellationRequested()) {
                return;
            }
            Callback(Cast<UClass>(TargetPath.ResolveObject()));
        }),
        ToAsyncLoadPriority(Priority)
    );

    if (RawHandle.IsValid() && CancellationToken.CanBeCancelled()) {
        TWeakPtr<FStreamableHandle> WeakHandle = RawHandle;
        RequestContext->TokenRegistration = CancellationToken.Register([WeakHandle]() {
            if (TSharedPtr<FStreamableHandle> Pinned = WeakHandle.Pin()) {
                Pinned->CancelHandle();
            }
        });
    }

    return FGameAssetLoadHandle(MoveTemp(RawHandle));
}

template <typename TAsset>
TGameCoroTask<TAsset*> FGameAssetLoader::LoadAsyncCoro(
    const TSoftObjectPtr<TAsset>& SoftAsset,
    UObject* WorldContextObject,
    FCancellationToken CancellationToken,
    EGameAssetPriority Priority
) {
    co_await Game::Coro::BindContext(WorldContextObject, CancellationToken);
    if (SoftAsset.IsNull() || CancellationToken.IsCancellationRequested()) {
        co_return nullptr;
    }

    if (TAsset* Resident = SoftAsset.Get()) {
        co_return Resident;
    }

    const FSoftObjectPath TargetPath = SoftAsset.ToSoftObjectPath();
    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestAsyncLoad(
        TargetPath,
        FStreamableDelegate(),
        ToAsyncLoadPriority(Priority)
    );

    if (RawHandle.IsValid()) {
        while (!RawHandle->HasLoadCompleted() && !CancellationToken.IsCancellationRequested()) {
            co_await Game::Coro::YieldTimeSlice(WorldContextObject);
        }
        if (CancellationToken.IsCancellationRequested()) {
            RawHandle->CancelHandle();
            co_return nullptr;
        }
    }

    TAsset* Loaded = (RawHandle.IsValid() && RawHandle->HasLoadCompleted())
        ? Cast<TAsset>(RawHandle->GetLoadedAsset())
        : Cast<TAsset>(TargetPath.ResolveObject());
    co_return Loaded;
}

template <typename TClass>
TGameCoroTask<UClass*> FGameAssetLoader::LoadClassAsyncCoro(
    const TSoftClassPtr<TClass>& SoftClass,
    UObject* WorldContextObject,
    FCancellationToken CancellationToken,
    EGameAssetPriority Priority
) {
    co_await Game::Coro::BindContext(WorldContextObject, CancellationToken);
    if (SoftClass.IsNull() || CancellationToken.IsCancellationRequested()) {
        co_return nullptr;
    }

    if (UClass* Resident = SoftClass.Get()) {
        co_return Resident;
    }

    const FSoftObjectPath TargetPath = SoftClass.ToSoftObjectPath();
    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestAsyncLoad(
        TargetPath,
        FStreamableDelegate(),
        ToAsyncLoadPriority(Priority)
    );

    if (RawHandle.IsValid()) {
        while (!RawHandle->HasLoadCompleted() && !CancellationToken.IsCancellationRequested()) {
            co_await Game::Coro::YieldTimeSlice(WorldContextObject);
        }
        if (CancellationToken.IsCancellationRequested()) {
            RawHandle->CancelHandle();
            co_return nullptr;
        }
    }

    UClass* Loaded = (RawHandle.IsValid() && RawHandle->HasLoadCompleted())
        ? Cast<UClass>(RawHandle->GetLoadedAsset())
        : Cast<UClass>(TargetPath.ResolveObject());
    co_return Loaded;
}

template <typename TAsset>
TGameCoroTask<TArray<TAsset*>> FGameAssetLoader::LoadBatchAsyncCoro(
    const TArray<TSoftObjectPtr<TAsset>>& SoftAssets,
    UObject* WorldContextObject,
    FCancellationToken CancellationToken,
    EGameAssetPriority Priority
) {
    co_await Game::Coro::BindContext(WorldContextObject, CancellationToken);
    if (SoftAssets.IsEmpty() || CancellationToken.IsCancellationRequested()) {
        co_return TArray<TAsset*>();
    }

    TArray<FSoftObjectPath> Paths;
    Paths.Reserve(SoftAssets.Num());
    TSet<FSoftObjectPath> UniquePaths;
    UniquePaths.Reserve(SoftAssets.Num());

    for (const TSoftObjectPtr<TAsset>& Asset : SoftAssets) {
        if (!Asset.IsNull()) {
            const FSoftObjectPath Path = Asset.ToSoftObjectPath();
            if (!UniquePaths.Contains(Path)) {
                UniquePaths.Add(Path);
                Paths.Add(Path);
            }
        }
    }

    if (Paths.IsEmpty()) {
        TArray<TAsset*> NullResults;
        NullResults.Init(nullptr, SoftAssets.Num());
        co_return NullResults;
    }

    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestAsyncLoad(
        MoveTemp(Paths),
        FStreamableDelegate(),
        ToAsyncLoadPriority(Priority)
    );

    if (RawHandle.IsValid()) {
        while (!RawHandle->HasLoadCompleted() && !CancellationToken.IsCancellationRequested()) {
            co_await Game::Coro::YieldTimeSlice(WorldContextObject);
        }
        if (CancellationToken.IsCancellationRequested()) {
            RawHandle->CancelHandle();
            co_return TArray<TAsset*>();
        }
    }

    TArray<TAsset*> Results;
    Results.Reserve(SoftAssets.Num());
    for (const TSoftObjectPtr<TAsset>& Asset : SoftAssets) {
        TAsset* Loaded = nullptr;
        if (!Asset.IsNull()) {
            Loaded = Cast<TAsset>(Asset.ToSoftObjectPath().ResolveObject());
        }
        Results.Add(Loaded);
    }
    co_return Results;
}

template <typename TAsset>
FGameAssetLoadHandle FGameAssetLoader::LoadBatchAsync(
    const TArray<TSoftObjectPtr<TAsset>>& SoftAssets,
    TWeakObjectPtr<UObject> Owner,
    TFunction<void()> OnAllLoaded,
    TFunction<void(float Progress)> OnProgress,
    const FCancellationToken& CancellationToken,
    EGameAssetPriority Priority
) {
    TArray<FSoftObjectPath> Paths;
    Paths.Reserve(SoftAssets.Num());
    for (const TSoftObjectPtr<TAsset>& Asset : SoftAssets) {
        if (!Asset.IsNull()) {
            Paths.Add(Asset.ToSoftObjectPath());
        }
    }
    return LoadBatchAsync(Paths, Owner, MoveTemp(OnAllLoaded), MoveTemp(OnProgress), CancellationToken, Priority);
}

template <typename TAsset>
TGameStatusResult<TAsset*> FGameAssetLoader::LoadSync(const TSoftObjectPtr<TAsset>& SoftAsset) {
    if (SoftAsset.IsNull()) {
        return TGameStatusResult<TAsset*>::Failure(
            FGameError::Make(Game::ModuleId::Asset, 1001, TEXT("SoftAsset is null or invalid"))
        );
    }

    if (TAsset* Resident = SoftAsset.Get()) {
        return TGameStatusResult<TAsset*>::Success(Resident);
    }

    const FSoftObjectPath TargetPath = SoftAsset.ToSoftObjectPath();
    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    const TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestSyncLoad(TargetPath);
    TAsset* Loaded = RawHandle.IsValid() ? Cast<TAsset>(RawHandle->GetLoadedAsset()) : Cast<TAsset>(TargetPath.ResolveObject());
    if (Loaded != nullptr) {
        return TGameStatusResult<TAsset*>::Success(Loaded);
    }

    return TGameStatusResult<TAsset*>::Failure(
        FGameError::Make(Game::ModuleId::Asset, 1002, TEXT("Failed to synchronously load asset: ") + TargetPath.ToString())
    );
}

template <typename TClass>
TGameStatusResult<UClass*> FGameAssetLoader::LoadClassSync(const TSoftClassPtr<TClass>& SoftClass) {
    if (SoftClass.IsNull()) {
        return TGameStatusResult<UClass*>::Failure(
            FGameError::Make(Game::ModuleId::Asset, 1001, TEXT("SoftClass is null or invalid"))
        );
    }

    if (UClass* Resident = SoftClass.Get()) {
        return TGameStatusResult<UClass*>::Success(Resident);
    }

    const FSoftObjectPath TargetPath = SoftClass.ToSoftObjectPath();
    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    const TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestSyncLoad(TargetPath);
    UClass* Loaded = RawHandle.IsValid() ? Cast<UClass>(RawHandle->GetLoadedAsset()) : Cast<UClass>(TargetPath.ResolveObject());
    if (Loaded != nullptr) {
        return TGameStatusResult<UClass*>::Success(Loaded);
    }

    return TGameStatusResult<UClass*>::Failure(
        FGameError::Make(Game::ModuleId::Asset, 1002, TEXT("Failed to synchronously load class: ") + TargetPath.ToString())
    );
}
