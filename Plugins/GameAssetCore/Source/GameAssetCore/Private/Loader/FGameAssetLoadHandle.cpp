/**
 * @file FGameAssetLoadHandle.cpp
 * @brief FGameAssetLoadHandle 异步资产流式加载控制句柄实现
 */

#include "Loader/FGameAssetLoadHandle.h"

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
}

void FGameAssetLoadHandle::Reset() {
    if (!StreamableHandle.IsValid()) {
        return;
    }
    if (StreamableHandle->IsActive()) {
        StreamableHandle->ReleaseHandle();
    }
    StreamableHandle.Reset();
}

bool FGameAssetLoadHandle::IsValid() const noexcept {
    return StreamableHandle.IsValid() && StreamableHandle->IsActive();
}

bool FGameAssetLoadHandle::IsLoading() const {
    return StreamableHandle.IsValid() && StreamableHandle->IsLoadingInProgress();
}

bool FGameAssetLoadHandle::HasCompleted() const {
    return StreamableHandle.IsValid() && !StreamableHandle->WasCanceled() && StreamableHandle->HasLoadCompleted();
}

bool FGameAssetLoadHandle::HasError() const {
    return StreamableHandle.IsValid() && StreamableHandle->HasError();
}

bool FGameAssetLoadHandle::WasCanceled() const {
    return StreamableHandle.IsValid() && StreamableHandle->WasCanceled();
}

float FGameAssetLoadHandle::GetProgress() const {
    if (!StreamableHandle.IsValid() || !StreamableHandle->IsActive()) {
        return 0.0f;
    }
    if (StreamableHandle->HasLoadCompleted()) {
        return 1.0f;
    }
    return StreamableHandle->GetProgress();
}

UObject* FGameAssetLoadHandle::GetLoadedAsset() const {
    if (!StreamableHandle.IsValid() || !StreamableHandle->IsActive() || !StreamableHandle->HasLoadCompleted()) {
        return nullptr;
    }
    return ::GetValid(StreamableHandle->GetLoadedAsset());
}

void FGameAssetLoadHandle::ForEachLoadedAsset(TFunctionRef<void(UObject*)> Visitor) const {
    if (!StreamableHandle.IsValid() || !StreamableHandle->IsActive() || !StreamableHandle->HasLoadCompleted()) {
        return;
    }
    StreamableHandle->ForEachLoadedAsset([&Visitor](UObject* LoadedAsset) {
        if (UObject* ValidAsset = ::GetValid(LoadedAsset)) {
            Visitor(ValidAsset);
        }
    });
}

void FGameAssetLoadHandle::GetLoadedAssets(TArray<UObject*>& OutLoadedAssets) const {
    OutLoadedAssets.Reset();
    if (!StreamableHandle.IsValid() || !StreamableHandle->IsActive() || !StreamableHandle->HasLoadCompleted()) {
        return;
    }
    int32 LoadedCount = 0;
    int32 RequestedCount = 0;
    StreamableHandle->GetLoadedCount(LoadedCount, RequestedCount);
    OutLoadedAssets.Reserve(RequestedCount);
    ForEachLoadedAsset([&OutLoadedAssets](UObject* ValidAsset) {
        OutLoadedAssets.Add(ValidAsset);
    });
}
