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

FScopedPinnedAsset::FScopedPinnedAsset(FSoftObjectPath InPath)
    : PinnedPath(MoveTemp(InPath)) {
    if (PinnedPath.IsValid()) {
        if (!FGameAssetLoader::PinAsset(PinnedPath)) {
            PinnedPath.Reset();
        }
    }
}

FScopedPinnedAsset::~FScopedPinnedAsset() {
    Release();
}

FScopedPinnedAsset::FScopedPinnedAsset(FScopedPinnedAsset&& Other) noexcept
    : PinnedPath(MoveTemp(Other.PinnedPath)) {
    Other.PinnedPath.Reset();
}

FScopedPinnedAsset& FScopedPinnedAsset::operator=(FScopedPinnedAsset&& Other) noexcept {
    if (this != &Other) {
        Release();
        PinnedPath = MoveTemp(Other.PinnedPath);
        Other.PinnedPath.Reset();
    }
    return *this;
}

void FScopedPinnedAsset::Release() noexcept {
    if (PinnedPath.IsValid()) {
        FGameAssetLoader::UnpinAsset(PinnedPath);
        PinnedPath.Reset();
    }
}

bool FScopedPinnedAsset::IsPinned() const noexcept {
    return PinnedPath.IsValid();
}

const FSoftObjectPath& FScopedPinnedAsset::GetPath() const noexcept {
    return PinnedPath;
}

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

    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestAsyncLoad(
        MoveTemp(ValidPaths),
        FStreamableDelegate::CreateLambda([Owner, Callback = MoveTemp(OnAllLoaded), OnProgress, CancellationToken]() mutable {
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
            CancellationToken.Register([WeakHandle]() {
                if (TSharedPtr<FStreamableHandle> Pinned = WeakHandle.Pin()) {
                    Pinned->CancelHandle();
                }
            });
        }
    }

    return FGameAssetLoadHandle(MoveTemp(RawHandle));
}

bool FGameAssetLoader::PinAsset(const FSoftObjectPath& AssetPath) {
    if (!AssetPath.IsValid()) {
        return false;
    }

    FScopeLock Lock(&GPinnedAssetsLock);
    if (FPinnedAssetEntry* Existing = GPinnedAssets.Find(AssetPath)) {
        ++Existing->RefCount;
        return true;
    }

    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    TSharedPtr<FStreamableHandle> Handle = Streamable.RequestSyncLoad(AssetPath);
    if (!Handle.IsValid() || Handle->HasError() || Handle->GetLoadedAsset() == nullptr) {
        return false;
    }

    FPinnedAssetEntry& Entry = GPinnedAssets.Add(AssetPath);
    Entry.Handle = MoveTemp(Handle);
    Entry.RefCount = 1;
    return true;
}

bool FGameAssetLoader::UnpinAsset(const FSoftObjectPath& AssetPath) {
    if (!AssetPath.IsValid()) {
        return false;
    }

    FScopeLock Lock(&GPinnedAssetsLock);
    if (FPinnedAssetEntry* Existing = GPinnedAssets.Find(AssetPath)) {
        --Existing->RefCount;
        if (Existing->RefCount <= 0) {
            GPinnedAssets.Remove(AssetPath);
        }
        return true;
    }

    return false;
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
