/**
 * @file UGameAssetBlueprintLibrary.cpp
 * @brief 资产常驻保活与状态查询蓝图函数库实现
 */

#include "Library/UGameAssetBlueprintLibrary.h"

#include "Asset/FGameAssetPinner.h"

bool UGameAssetBlueprintLibrary::IsAssetPinned(const TSoftObjectPtr<UObject>& Asset) {
    return FGameAssetPinner::IsAssetPinned(Asset.ToSoftObjectPath());
}

bool UGameAssetBlueprintLibrary::PinAsset(const TSoftObjectPtr<UObject>& Asset) {
    return FGameAssetPinner::PinAsset(Asset.ToSoftObjectPath()).IsSuccess();
}

bool UGameAssetBlueprintLibrary::UnpinAsset(const TSoftObjectPtr<UObject>& Asset) {
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
    return FGameAssetPinner::GetPinRefCount(Asset.ToSoftObjectPath());
}

UObject* UGameAssetBlueprintLibrary::GetPinnedAsset(const TSoftObjectPtr<UObject>& Asset) {
    return FGameAssetPinner::GetPinnedAsset(Asset.ToSoftObjectPath());
}

bool UGameAssetBlueprintLibrary::IsAssetLoaded(const TSoftObjectPtr<UObject>& Asset) {
    return Asset.Get() != nullptr || Asset.ToSoftObjectPath().ResolveObject() != nullptr;
}

UObject* UGameAssetBlueprintLibrary::GetLoadedAsset(const TSoftObjectPtr<UObject>& Asset) {
    if (UObject* Direct = Asset.Get()) {
        return Direct;
    }
    return Asset.ToSoftObjectPath().ResolveObject();
}

int32 UGameAssetBlueprintLibrary::GetTotalPinnedAssetCount() {
    return FGameAssetPinner::GetTotalPinnedCount();
}

TArray<FSoftObjectPath> UGameAssetBlueprintLibrary::GetAllPinnedAssetPaths() {
    TArray<FSoftObjectPath> Paths;
    FGameAssetPinner::GetAllPinnedPaths(Paths);
    return Paths;
}
