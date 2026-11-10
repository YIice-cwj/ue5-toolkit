/**
 * @file FGameAssetLoader.cpp
 * @brief FGameAssetLoader 弱引用守卫异步资产加载器与内存保活实现
 */

#include "Asset/FGameAssetLoader.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

namespace {

struct FPinnedAssetEntry {
    TSharedPtr<FStreamableHandle> Handle;
    int32 RefCount = 0;
};

FCriticalSection GPinnedAssetsLock;
TMap<FSoftObjectPath, FPinnedAssetEntry> GPinnedAssets;

} // namespace

FGameAssetLoadHandle FGameAssetLoader::LoadBatchAsync(
    const TArray<FSoftObjectPath>& AssetPaths,
    TWeakObjectPtr<UObject> Owner,
    TFunction<void()> OnAllLoaded,
    TAsyncLoadPriority Priority
) {
    return LoadBatchAsync(AssetPaths, Owner, MoveTemp(OnAllLoaded), nullptr, FCancellationToken::None(), Priority);
}

FGameAssetLoadHandle FGameAssetLoader::LoadBatchAsync(
    const TArray<FSoftObjectPath>& AssetPaths,
    TWeakObjectPtr<UObject> Owner,
    TFunction<void()> OnAllLoaded,
    TFunction<void(float Progress)> OnProgress,
    const FCancellationToken& CancellationToken,
    TAsyncLoadPriority Priority
) {
    if (!Owner.IsValid() || !OnAllLoaded || CancellationToken.IsCancellationRequested()) {
        return FGameAssetLoadHandle();
    }

    TArray<FSoftObjectPath> ValidPaths;
    ValidPaths.Reserve(AssetPaths.Num());
    for (const FSoftObjectPath& Path : AssetPaths) {
        if (Path.IsValid()) {
            ValidPaths.AddUnique(Path);
        }
    }

    if (ValidPaths.IsEmpty()) {
        if (OnProgress) {
            OnProgress(1.0f);
        }
        OnAllLoaded();
        return FGameAssetLoadHandle();
    }

    bool bAllResident = true;
    for (const FSoftObjectPath& Path : ValidPaths) {
        if (Path.ResolveObject() == nullptr) {
            bAllResident = false;
            break;
        }
    }

    if (bAllResident) {
        if (OnProgress) {
            OnProgress(1.0f);
        }
        OnAllLoaded();
        return FGameAssetLoadHandle();
    }

    struct FBatchRequestContext {
        FCancellationTokenRegistration TokenRegistration;
    };
    TSharedRef<FBatchRequestContext> RequestContext = MakeShared<FBatchRequestContext>();

    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestAsyncLoad(
        MoveTemp(ValidPaths),
        FStreamableDelegate::CreateLambda([Owner, Callback = MoveTemp(OnAllLoaded), OnProgress, CancellationToken, RequestContext]() mutable {
            if (!Owner.IsValid() || CancellationToken.IsCancellationRequested()) {
                return;
            }
            if (OnProgress) {
                OnProgress(1.0f);
            }
            Callback();
        }),
        Priority
    );

    if (RawHandle.IsValid()) {
        if (OnProgress) {
            RawHandle->BindUpdateDelegate(FStreamableUpdateDelegate::CreateLambda(
                [Owner, OnProgress, CancellationToken](TSharedRef<FStreamableHandle> InHandle) {
                    if (Owner.IsValid() && !CancellationToken.IsCancellationRequested()) {
                        OnProgress(InHandle->GetProgress());
                    }
                }
            ));
        }

        if (CancellationToken.CanBeCancelled()) {
            TWeakPtr<FStreamableHandle> WeakHandle = RawHandle;
            RequestContext->TokenRegistration = CancellationToken.Register([WeakHandle]() {
                if (TSharedPtr<FStreamableHandle> Pinned = WeakHandle.Pin()) {
                    Pinned->CancelHandle();
                }
            });
        }
    }

    return FGameAssetLoadHandle(MoveTemp(RawHandle));
}

TGameStatusResult<void> FGameAssetLoader::PinAsset(const FSoftObjectPath& AssetPath) {
    if (!AssetPath.IsValid()) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Asset, 1001, TEXT("AssetPath is invalid"))
        );
    }

    FScopeLock Lock(&GPinnedAssetsLock);
    if (FPinnedAssetEntry* Existing = GPinnedAssets.Find(AssetPath)) {
        ++Existing->RefCount;
        return TGameStatusResult<void>::Success();
    }

    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    TSharedPtr<FStreamableHandle> Handle = Streamable.RequestSyncLoad(AssetPath);
    if (!Handle.IsValid() || Handle->HasError() || Handle->GetLoadedAsset() == nullptr) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Asset, 1002, TEXT("Failed to pin asset due to sync load failure"))
        );
    }

    FPinnedAssetEntry& Entry = GPinnedAssets.Add(AssetPath);
    Entry.Handle = MoveTemp(Handle);
    Entry.RefCount = 1;
    return TGameStatusResult<void>::Success();
}

TGameStatusResult<void> FGameAssetLoader::UnpinAsset(const FSoftObjectPath& AssetPath) {
    if (!AssetPath.IsValid()) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Asset, 1001, TEXT("AssetPath is invalid"))
        );
    }

    FScopeLock Lock(&GPinnedAssetsLock);
    if (FPinnedAssetEntry* Existing = GPinnedAssets.Find(AssetPath)) {
        --Existing->RefCount;
        if (Existing->RefCount <= 0) {
            GPinnedAssets.Remove(AssetPath);
        }
        return TGameStatusResult<void>::Success();
    }

    return TGameStatusResult<void>::Failure(
        FGameError::Make(Game::ModuleId::Asset, 1003, TEXT("Asset is not pinned"))
    );
}

bool FGameAssetLoader::IsAssetPinned(const FSoftObjectPath& AssetPath) {
    if (!AssetPath.IsValid()) {
        return false;
    }

    FScopeLock Lock(&GPinnedAssetsLock);
    return GPinnedAssets.Contains(AssetPath);
}

void FGameAssetLoader::ClearAllPinnedAssets() {
    FScopeLock Lock(&GPinnedAssetsLock);
    GPinnedAssets.Empty();
}
