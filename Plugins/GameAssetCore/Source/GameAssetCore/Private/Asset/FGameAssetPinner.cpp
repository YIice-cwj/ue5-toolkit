/**
 * @file FGameAssetPinner.cpp
 * @brief FGameAssetPinner 资产内存常驻锁定与引用计数管理器实现
 */

#include "Asset/FGameAssetPinner.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
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

TGameStatusResult<void> FGameAssetPinner::PinAsset(const FSoftObjectPath& AssetPath) {
    if (!AssetPath.IsValid()) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Asset, 1001, TEXT("AssetPath is invalid"))
        );
    }

    {
        FScopeLock Lock(&GPinnedAssetsLock);
        if (FPinnedAssetEntry* Existing = GPinnedAssets.Find(AssetPath)) {
            ++Existing->RefCount;
            return TGameStatusResult<void>::Success();
        }
    }

    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    TSharedPtr<FStreamableHandle> Handle = Streamable.RequestSyncLoad(AssetPath);
    const bool bValidLoad = Handle.IsValid() && !Handle->HasError() && (Handle->GetLoadedAsset() != nullptr || AssetPath.ResolveObject() != nullptr);
    if (!bValidLoad) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Asset, 1002, TEXT("Failed to pin asset due to sync load failure"))
        );
    }

    {
        FScopeLock Lock(&GPinnedAssetsLock);
        if (FPinnedAssetEntry* Existing = GPinnedAssets.Find(AssetPath)) {
            ++Existing->RefCount;
            return TGameStatusResult<void>::Success();
        }

        FPinnedAssetEntry& Entry = GPinnedAssets.Add(AssetPath);
        Entry.Handle = MoveTemp(Handle);
        Entry.RefCount = 1;
    }
    return TGameStatusResult<void>::Success();
}

TGameStatusResult<void> FGameAssetPinner::UnpinAsset(const FSoftObjectPath& AssetPath) {
    if (!AssetPath.IsValid()) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Asset, 1001, TEXT("AssetPath is invalid"))
        );
    }

    FPinnedAssetEntry ReleasedEntry;
    bool bSuccess = false;
    {
        FScopeLock Lock(&GPinnedAssetsLock);
        if (FPinnedAssetEntry* Existing = GPinnedAssets.Find(AssetPath)) {
            --Existing->RefCount;
            if (Existing->RefCount <= 0) {
                GPinnedAssets.RemoveAndCopyValue(AssetPath, ReleasedEntry);
            }
            bSuccess = true;
        }
    }

    if (bSuccess) {
        return TGameStatusResult<void>::Success();
    }

    return TGameStatusResult<void>::Failure(
        FGameError::Make(Game::ModuleId::Asset, 1003, TEXT("Asset is not pinned"))
    );
}

bool FGameAssetPinner::IsAssetPinned(const FSoftObjectPath& AssetPath) {
    if (!AssetPath.IsValid()) {
        return false;
    }

    FScopeLock Lock(&GPinnedAssetsLock);
    return GPinnedAssets.Contains(AssetPath);
}

int32 FGameAssetPinner::GetPinRefCount(const FSoftObjectPath& AssetPath) {
    if (!AssetPath.IsValid()) {
        return 0;
    }

    FScopeLock Lock(&GPinnedAssetsLock);
    if (const FPinnedAssetEntry* Existing = GPinnedAssets.Find(AssetPath)) {
        return Existing->RefCount;
    }
    return 0;
}

UObject* FGameAssetPinner::GetPinnedAsset(const FSoftObjectPath& AssetPath) {
    if (!AssetPath.IsValid()) {
        return nullptr;
    }

    FScopeLock Lock(&GPinnedAssetsLock);
    if (const FPinnedAssetEntry* Existing = GPinnedAssets.Find(AssetPath)) {
        return Existing->Handle.IsValid() ? Existing->Handle->GetLoadedAsset() : AssetPath.ResolveObject();
    }
    return nullptr;
}

int32 FGameAssetPinner::GetTotalPinnedCount() {
    FScopeLock Lock(&GPinnedAssetsLock);
    return GPinnedAssets.Num();
}

void FGameAssetPinner::GetAllPinnedPaths(TArray<FSoftObjectPath>& OutPaths) {
    FScopeLock Lock(&GPinnedAssetsLock);
    OutPaths.Reset(GPinnedAssets.Num());
    for (const auto& Pair : GPinnedAssets) {
        OutPaths.Add(Pair.Key);
    }
}

void FGameAssetPinner::ClearAllPinnedAssets() {
    TMap<FSoftObjectPath, FPinnedAssetEntry> OldAssets;
    {
        FScopeLock Lock(&GPinnedAssetsLock);
        OldAssets = MoveTemp(GPinnedAssets);
    }
}
