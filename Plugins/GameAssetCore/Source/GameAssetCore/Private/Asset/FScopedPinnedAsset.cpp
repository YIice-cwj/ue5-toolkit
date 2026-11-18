/**
 * @file FScopedPinnedAsset.cpp
 * @brief FScopedPinnedAsset 内存保活作用域守卫实现
 */

#include "Asset/FScopedPinnedAsset.h"
#include "Asset/FGameAssetPinner.h"

FScopedPinnedAsset::FScopedPinnedAsset(FSoftObjectPath InPath)
    : PinnedPath(MoveTemp(InPath))
    , CachedAsset(nullptr) {
    if (PinnedPath.IsValid()) {
        if (FGameAssetPinner::PinAsset(PinnedPath)) {
            CachedAsset = FGameAssetPinner::GetPinnedAsset(PinnedPath);
        } else {
            PinnedPath.Reset();
        }
    }
}

FScopedPinnedAsset::~FScopedPinnedAsset() {
    Release();
}

FScopedPinnedAsset::FScopedPinnedAsset(FScopedPinnedAsset&& Other) noexcept
    : PinnedPath(MoveTemp(Other.PinnedPath))
    , CachedAsset(Other.CachedAsset) {
    Other.PinnedPath.Reset();
    Other.CachedAsset = nullptr;
}

FScopedPinnedAsset& FScopedPinnedAsset::operator=(FScopedPinnedAsset&& Other) noexcept {
    if (this != &Other) {
        Release();
        PinnedPath = MoveTemp(Other.PinnedPath);
        CachedAsset = Other.CachedAsset;
        Other.PinnedPath.Reset();
        Other.CachedAsset = nullptr;
    }
    return *this;
}

void FScopedPinnedAsset::Release() noexcept {
    if (PinnedPath.IsValid()) {
        FGameAssetPinner::UnpinAsset(PinnedPath);
        PinnedPath.Reset();
        CachedAsset = nullptr;
    }
}

bool FScopedPinnedAsset::IsPinned() const noexcept {
    return PinnedPath.IsValid();
}

const FSoftObjectPath& FScopedPinnedAsset::GetPath() const noexcept {
    return PinnedPath;
}

UObject* FScopedPinnedAsset::Get() const noexcept {
    return CachedAsset;
}
