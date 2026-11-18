/**
 * @file FGameAssetLoadHandle.cpp
 * @brief FGameAssetLoadHandle 异步资产流式加载控制句柄实现
 */

#include "Asset/FGameAssetLoadHandle.h"

#include "Engine/StreamableManager.h"

FGameAssetLoadHandle::FGameAssetLoadHandle(TSharedPtr<FStreamableHandle> InHandle) noexcept
    : StreamableHandle(MoveTemp(InHandle)) {
}

void FGameAssetLoadHandle::Cancel() {
    if (!StreamableHandle.IsValid()) {
        return;
    }
    if (StreamableHandle->IsActive()) {
        StreamableHandle->CancelHandle();
    }
    StreamableHandle.Reset();
}

void FGameAssetLoadHandle::Reset() {
    if (!StreamableHandle.IsValid()) {
        return;
    }
    StreamableHandle->ReleaseHandle();
    StreamableHandle.Reset();
}

bool FGameAssetLoadHandle::IsValid() const noexcept {
    return StreamableHandle.IsValid() && !StreamableHandle->WasCanceled();
}

bool FGameAssetLoadHandle::IsLoading() const {
    return StreamableHandle.IsValid() && StreamableHandle->IsLoadingInProgress();
}

bool FGameAssetLoadHandle::HasCompleted() const {
    return StreamableHandle.IsValid() && StreamableHandle->HasLoadCompleted();
}

bool FGameAssetLoadHandle::HasError() const {
    return StreamableHandle.IsValid() && StreamableHandle->HasError();
}

bool FGameAssetLoadHandle::WasCanceled() const {
    return StreamableHandle.IsValid() && StreamableHandle->WasCanceled();
}

float FGameAssetLoadHandle::GetProgress() const {
    if (!StreamableHandle.IsValid() || StreamableHandle->WasCanceled()) {
        return 0.0f;
    }
    if (StreamableHandle->HasLoadCompleted()) {
        return 1.0f;
    }
    return StreamableHandle->GetProgress();
}

UObject* FGameAssetLoadHandle::GetLoadedAsset() const {
    if (!StreamableHandle.IsValid() || StreamableHandle->WasCanceled() || !StreamableHandle->HasLoadCompleted()) {
        return nullptr;
    }
    return StreamableHandle->GetLoadedAsset();
}

void FGameAssetLoadHandle::GetLoadedAssets(TArray<UObject*>& OutLoadedAssets) const {
    OutLoadedAssets.Reset();
    if (StreamableHandle.IsValid() && !StreamableHandle->WasCanceled() && StreamableHandle->HasLoadCompleted()) {
        StreamableHandle->GetLoadedAssets(OutLoadedAssets);
    }
}
