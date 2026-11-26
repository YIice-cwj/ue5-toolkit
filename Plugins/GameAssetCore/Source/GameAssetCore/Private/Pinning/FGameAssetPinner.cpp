/**
 * @file FGameAssetPinner.cpp
 * @brief FGameAssetPinner 资产内存常驻锁定、SIOF 免疫存储与 FGCObject 强引用保活实现
 */

#include "Pinning/FGameAssetPinner.h"

#include <cstddef>
#include <memory>

#include "Async/FGameThreadTaskRunner.h"
#include "Core/AssetErrorCodes.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Logging/GameLogCategories.h"
#include "Misc/ScopeLock.h"
#include "Pinning/FPinnedAssetStorage.h"

using Game::Asset::Private::FPinnedAssetStorage;

namespace {

FPinnedAssetStorage& GetPinnedStorage() {
    alignas(FPinnedAssetStorage) static std::byte StorageBuffer[sizeof(FPinnedAssetStorage)];
    static FPinnedAssetStorage* Instance = std::construct_at(reinterpret_cast<FPinnedAssetStorage*>(StorageBuffer));
    return *Instance;
}

void ReleaseStreamableHandleOnGameThread(TSharedPtr<FStreamableHandle> InHandle) {
    if (!InHandle.IsValid()) {
        return;
    }
    if (IsInGameThread()) {
        if (InHandle->IsActive()) {
            InHandle->ReleaseHandle();
        }
        return;
    }
    FGameThreadTaskRunner::RunOnGameThread([Handle = MoveTemp(InHandle)]() mutable {
        if (Handle.IsValid() && Handle->IsActive()) {
            Handle->ReleaseHandle();
        }
    });
}

void ReleaseStreamableHandlesOnGameThread(TArray<TSharedPtr<FStreamableHandle>> InHandles) {
    if (InHandles.IsEmpty()) {
        return;
    }
    if (IsInGameThread()) {
        for (TSharedPtr<FStreamableHandle>& Handle : InHandles) {
            if (Handle.IsValid() && Handle->IsActive()) {
                Handle->ReleaseHandle();
            }
        }
        return;
    }
    FGameThreadTaskRunner::RunOnGameThread([Handles = MoveTemp(InHandles)]() mutable {
        for (TSharedPtr<FStreamableHandle>& Handle : Handles) {
            if (Handle.IsValid() && Handle->IsActive()) {
                Handle->ReleaseHandle();
            }
        }
    });
}

}  // namespace

void FGameAssetPinner::Startup() {
    GetPinnedStorage().EnsureGCReferencer();
}

void FGameAssetPinner::Shutdown() {
    FPinnedAssetStorage& Storage = GetPinnedStorage();
    TArray<TSharedPtr<FStreamableHandle>> HandlesToRelease;
    {
        FScopeLock ScopeLock(&Storage.Lock);
        HandlesToRelease.Reserve(Storage.PinnedAssets.Num());
        for (TPair<FSoftObjectPath, FPinnedAssetStorage::FEntry>& Pair : Storage.PinnedAssets) {
            if (Pair.Value.Handle.IsValid()) {
                HandlesToRelease.Add(MoveTemp(Pair.Value.Handle));
            }
        }
        Storage.PinnedAssets.Reset();
    }
    Storage.ResetGCReferencer();
    ReleaseStreamableHandlesOnGameThread(MoveTemp(HandlesToRelease));
}

TGameStatusResult<UObject*> FGameAssetPinner::PinAsset(const FSoftObjectPath& AssetPath) {
    if (!AssetPath.IsValid()) {
        return TGameStatusResult<UObject*>::Failure(
            Game::ModuleId::Asset,
            Game::AssetErrorCodes::InvalidAssetPath,
            TEXT("AssetPath is null or invalid.")
        );
    }

    FPinnedAssetStorage& Storage = GetPinnedStorage();
    TSharedPtr<FStreamableHandle> StaleHandle;
    {
        FScopeLock ScopeLock(&Storage.Lock);
        if (FPinnedAssetStorage::FEntry* Existing = Storage.PinnedAssets.Find(AssetPath)) {
            UObject* ExistingAsset = ::GetValid(Existing->Asset.Get());
            if (ExistingAsset != nullptr) {
                ++Existing->RefCount;
                return TGameStatusResult<UObject*>::Success(ExistingAsset);
            }
            StaleHandle = MoveTemp(Existing->Handle);
            Storage.PinnedAssets.Remove(AssetPath);
        }
    }
    ReleaseStreamableHandleOnGameThread(MoveTemp(StaleHandle));

    UObject* LoadedAsset = ::GetValid(AssetPath.ResolveObject());
    TSharedPtr<FStreamableHandle> SyncHandle;

    if (LoadedAsset == nullptr) {
        if (!IsInGameThread()) {
            const FString PathText = AssetPath.ToString();
            GAME_LOG(Warning, TEXT("【资产常驻】拒绝在非游戏线程对未驻留资产执行同步磁盘加载: %s"), *PathText);
            return TGameStatusResult<UObject*>::Failure(
                Game::ModuleId::Asset,
                Game::AssetErrorCodes::InvalidThread,
                FString::Printf(TEXT("Cannot synchronously load unresident asset '%s' off the GameThread."), *PathText)
            );
        }

        if (UAssetManager::IsInitialized()) {
            FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
            SyncHandle = Streamable.RequestSyncLoad(AssetPath);
            if (SyncHandle.IsValid() && !SyncHandle->HasError()) {
                LoadedAsset = ::GetValid(SyncHandle->GetLoadedAsset());
            }
        }
        if (LoadedAsset == nullptr) {
            LoadedAsset = ::GetValid(AssetPath.ResolveObject());
        }
        if (LoadedAsset == nullptr) {
            ReleaseStreamableHandleOnGameThread(MoveTemp(SyncHandle));
            const FString PathText = AssetPath.ToString();
            GAME_LOG(Warning, TEXT("【资产常驻】同步加载目标资产失败，路径不存在或包体损坏: %s"), *PathText);
            return TGameStatusResult<UObject*>::Failure(
                Game::ModuleId::Asset,
                Game::AssetErrorCodes::LoadFailed,
                FString::Printf(TEXT("Failed to load asset '%s' for memory pinning."), *PathText)
            );
        }
    }

    Storage.EnsureGCReferencer();

    bool bNewlyPinned = false;
    UObject* FinalPinnedAsset = LoadedAsset;
    TSharedPtr<FStreamableHandle> HandleToRelease;
    {
        FScopeLock ScopeLock(&Storage.Lock);

        if (FPinnedAssetStorage::FEntry* Existing = Storage.PinnedAssets.Find(AssetPath)) {
            UObject* ExistingAsset = ::GetValid(Existing->Asset.Get());
            if (ExistingAsset != nullptr) {
                ++Existing->RefCount;
                FinalPinnedAsset = ExistingAsset;
                HandleToRelease = MoveTemp(SyncHandle);
            } else {
                HandleToRelease = MoveTemp(Existing->Handle);
                Existing->Asset = LoadedAsset;
                Existing->Handle = MoveTemp(SyncHandle);
                Existing->RefCount = 1;
                bNewlyPinned = true;
            }
        } else {
            FPinnedAssetStorage::FEntry& NewEntry = Storage.PinnedAssets.Add(AssetPath);
            NewEntry.Asset = LoadedAsset;
            NewEntry.Handle = MoveTemp(SyncHandle);
            NewEntry.RefCount = 1;
            bNewlyPinned = true;
        }
    }

    ReleaseStreamableHandleOnGameThread(MoveTemp(HandleToRelease));

    if (bNewlyPinned) {
        GAME_LOG(Verbose, TEXT("【资产常驻】首次建立资产常驻保活: %s"), *AssetPath.ToString());
    }
    return TGameStatusResult<UObject*>::Success(FinalPinnedAsset);
}

TGameStatusResult<void> FGameAssetPinner::UnpinAsset(const FSoftObjectPath& AssetPath) {
    if (!AssetPath.IsValid()) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Asset,
            Game::AssetErrorCodes::InvalidAssetPath,
            TEXT("AssetPath is null or invalid.")
        );
    }

    TSharedPtr<FStreamableHandle> ReleasedHandle;
    bool bFound = false;
    bool bFullyReleased = false;
    {
        FPinnedAssetStorage& Storage = GetPinnedStorage();
        FScopeLock ScopeLock(&Storage.Lock);
        if (FPinnedAssetStorage::FEntry* Existing = Storage.PinnedAssets.Find(AssetPath)) {
            bFound = true;
            --Existing->RefCount;
            if (Existing->RefCount <= 0) {
                ReleasedHandle = MoveTemp(Existing->Handle);
                Storage.PinnedAssets.Remove(AssetPath);
                bFullyReleased = true;
            }
        }
    }

    if (!bFound) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Asset,
            Game::AssetErrorCodes::AssetNotPinned,
            FString::Printf(TEXT("Asset '%s' is not currently pinned."), *AssetPath.ToString())
        );
    }

    if (bFullyReleased) {
        ReleaseStreamableHandleOnGameThread(MoveTemp(ReleasedHandle));
        GAME_LOG(Verbose, TEXT("【资产常驻】引用计数归零，解除资产常驻保活: %s"), *AssetPath.ToString());
    }
    return TGameStatusResult<void>::Success();
}

bool FGameAssetPinner::IsAssetPinned(const FSoftObjectPath& AssetPath) {
    if (!AssetPath.IsValid()) {
        return false;
    }

    FPinnedAssetStorage& Storage = GetPinnedStorage();
    FScopeLock ScopeLock(&Storage.Lock);
    const FPinnedAssetStorage::FEntry* Existing = Storage.PinnedAssets.Find(AssetPath);
    return Existing != nullptr && ::GetValid(Existing->Asset.Get()) != nullptr;
}

int32 FGameAssetPinner::GetPinRefCount(const FSoftObjectPath& AssetPath) {
    if (!AssetPath.IsValid()) {
        return 0;
    }

    FPinnedAssetStorage& Storage = GetPinnedStorage();
    FScopeLock ScopeLock(&Storage.Lock);
    const FPinnedAssetStorage::FEntry* Existing = Storage.PinnedAssets.Find(AssetPath);
    if (Existing != nullptr && ::GetValid(Existing->Asset.Get()) != nullptr) {
        return Existing->RefCount;
    }
    return 0;
}

UObject* FGameAssetPinner::GetPinnedAsset(const FSoftObjectPath& AssetPath) {
    if (!AssetPath.IsValid()) {
        return nullptr;
    }

    FPinnedAssetStorage& Storage = GetPinnedStorage();
    FScopeLock ScopeLock(&Storage.Lock);
    if (const FPinnedAssetStorage::FEntry* Existing = Storage.PinnedAssets.Find(AssetPath)) {
        return ::GetValid(Existing->Asset.Get());
    }
    return nullptr;
}

int32 FGameAssetPinner::GetTotalPinnedCount() {
    FPinnedAssetStorage& Storage = GetPinnedStorage();
    FScopeLock ScopeLock(&Storage.Lock);
    int32 ValidCount = 0;
    for (const TPair<FSoftObjectPath, FPinnedAssetStorage::FEntry>& Pair : Storage.PinnedAssets) {
        if (::GetValid(Pair.Value.Asset.Get()) != nullptr) {
            ++ValidCount;
        }
    }
    return ValidCount;
}

void FGameAssetPinner::GetAllPinnedPaths(TArray<FSoftObjectPath>& OutPaths) {
    FPinnedAssetStorage& Storage = GetPinnedStorage();
    FScopeLock ScopeLock(&Storage.Lock);
    OutPaths.Reset(Storage.PinnedAssets.Num());
    for (const TPair<FSoftObjectPath, FPinnedAssetStorage::FEntry>& Pair : Storage.PinnedAssets) {
        if (::GetValid(Pair.Value.Asset.Get()) != nullptr) {
            OutPaths.Add(Pair.Key);
        }
    }
}

void FGameAssetPinner::ClearAllPinnedAssets() {
    TArray<TSharedPtr<FStreamableHandle>> HandlesToRelease;
    int32 ClearedCount = 0;
    {
        FPinnedAssetStorage& Storage = GetPinnedStorage();
        FScopeLock ScopeLock(&Storage.Lock);
        ClearedCount = Storage.PinnedAssets.Num();
        HandlesToRelease.Reserve(ClearedCount);
        for (TPair<FSoftObjectPath, FPinnedAssetStorage::FEntry>& Pair : Storage.PinnedAssets) {
            if (Pair.Value.Handle.IsValid()) {
                HandlesToRelease.Add(MoveTemp(Pair.Value.Handle));
            }
        }
        Storage.PinnedAssets.Reset();
    }

    if (ClearedCount > 0) {
        ReleaseStreamableHandlesOnGameThread(MoveTemp(HandlesToRelease));
        GAME_LOG(Verbose, TEXT("【资产常驻】已清空全部常驻资产，释放条目数: %d"), ClearedCount);
    }
}
