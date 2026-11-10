/**
 * @file FScopedPinnedAsset.cpp
 * @brief FScopedPinnedAsset 内存保活作用域守卫实现
 */

#include "Asset/FScopedPinnedAsset.h"
#include "Asset/FGameAssetLoader.h"

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
