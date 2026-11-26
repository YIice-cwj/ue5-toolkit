/**
 * @file UGameAssetBlueprintLibrary.cpp
 * @brief UGameAssetBlueprintLibrary 资产常驻保活与免阻塞状态查询蓝图函数库实现
 */

#include "Blueprint/UGameAssetBlueprintLibrary.h"

#include "Pinning/FGameAssetPinner.h"

bool UGameAssetBlueprintLibrary::IsAssetPinned(const TSoftObjectPtr<UObject>& Asset) {
    if (Asset.IsNull()) {
        return false;
    }
    return FGameAssetPinner::IsAssetPinned(Asset.ToSoftObjectPath());
}

bool UGameAssetBlueprintLibrary::PinAsset(const TSoftObjectPtr<UObject>& Asset) {
    if (Asset.IsNull()) {
        return false;
    }
    return FGameAssetPinner::PinAsset(Asset.ToSoftObjectPath()).IsSuccess();
}

bool UGameAssetBlueprintLibrary::UnpinAsset(const TSoftObjectPtr<UObject>& Asset) {
    if (Asset.IsNull()) {
        return false;
    }
    return FGameAssetPinner::UnpinAsset(Asset.ToSoftObjectPath()).IsSuccess();
}

int32 UGameAssetBlueprintLibrary::PinAssets(const TArray<TSoftObjectPtr<UObject>>& Assets) {
    int32 SuccessCount = 0;
    for (const TSoftObjectPtr<UObject>& Asset : Assets) {
        if (PinAsset(Asset)) {
            ++SuccessCount;
        }
    }
    return SuccessCount;
}

int32 UGameAssetBlueprintLibrary::UnpinAssets(const TArray<TSoftObjectPtr<UObject>>& Assets) {
    int32 SuccessCount = 0;
    for (const TSoftObjectPtr<UObject>& Asset : Assets) {
        if (UnpinAsset(Asset)) {
            ++SuccessCount;
        }
    }
    return SuccessCount;
}

int32 UGameAssetBlueprintLibrary::GetAssetPinRefCount(const TSoftObjectPtr<UObject>& Asset) {
    if (Asset.IsNull()) {
        return 0;
    }
    return FGameAssetPinner::GetPinRefCount(Asset.ToSoftObjectPath());
}

UObject* UGameAssetBlueprintLibrary::GetPinnedAsset(const TSoftObjectPtr<UObject>& Asset) {
    if (Asset.IsNull()) {
        return nullptr;
    }
    return FGameAssetPinner::GetPinnedAsset(Asset.ToSoftObjectPath());
}

bool UGameAssetBlueprintLibrary::IsAssetLoaded(const TSoftObjectPtr<UObject>& Asset) {
    return Asset.Get() != nullptr;
}

UObject* UGameAssetBlueprintLibrary::GetLoadedAsset(const TSoftObjectPtr<UObject>& Asset) {
    return Asset.Get();
}

int32 UGameAssetBlueprintLibrary::GetTotalPinnedAssetCount() {
    return FGameAssetPinner::GetTotalPinnedCount();
}

TArray<FSoftObjectPath> UGameAssetBlueprintLibrary::GetAllPinnedAssetPaths() {
    TArray<FSoftObjectPath> Paths;
    FGameAssetPinner::GetAllPinnedPaths(Paths);
    return Paths;
}
