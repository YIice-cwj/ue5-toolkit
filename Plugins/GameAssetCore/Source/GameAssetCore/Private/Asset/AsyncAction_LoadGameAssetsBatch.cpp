/**
 * @file AsyncAction_LoadGameAssetsBatch.cpp
 * @brief UAsyncAction_LoadGameAssetsBatch 软引用资产批量异步流式加载蓝图动作节点实现
 */

#include "Asset/AsyncAction_LoadGameAssetsBatch.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

UAsyncAction_LoadGameAssetsBatch* UAsyncAction_LoadGameAssetsBatch::LoadGameAssetsBatchAsync(
    UObject* WorldContextObject,
    const TArray<TSoftObjectPtr<UObject>>& Assets,
    EGameAssetPriority Priority
) {
    UAsyncAction_LoadGameAssetsBatch* Action = NewObject<UAsyncAction_LoadGameAssetsBatch>();
    Action->WorldContext = WorldContextObject;
    Action->TargetAssets = Assets;
    Action->LoadPriority = Priority;
    Action->RegisterWithGameInstance(WorldContextObject);
    return Action;
}

void UAsyncAction_LoadGameAssetsBatch::Activate() {
    if (!WorldContext.IsValid() || TargetAssets.IsEmpty()) {
        OnFailure.Broadcast();
        OnFailureNative.Broadcast();
        SetReadyToDestroy();
        return;
    }

    bool bAllResident = true;
    TArray<UObject*> ResidentAssets;
    ResidentAssets.Reserve(TargetAssets.Num());
    TArray<FSoftObjectPath> TargetPaths;
    TargetPaths.Reserve(TargetAssets.Num());

    for (const TSoftObjectPtr<UObject>& Asset : TargetAssets) {
        if (Asset.IsNull()) {
            OnFailure.Broadcast();
            OnFailureNative.Broadcast();
            SetReadyToDestroy();
            return;
        }

        UObject* Resolved = Asset.Get();
        if (!Resolved) {
            Resolved = Asset.ToSoftObjectPath().ResolveObject();
        }

        if (Resolved != nullptr) {
            ResidentAssets.Add(Resolved);
        } else {
            bAllResident = false;
        }
        TargetPaths.Add(Asset.ToSoftObjectPath());
    }

    if (bAllResident) {
        OnProgress.Broadcast(1.0f);
        OnProgressNative.Broadcast(1.0f);
        OnSuccess.Broadcast(ResidentAssets);
        OnSuccessNative.Broadcast(ResidentAssets);
        SetReadyToDestroy();
        return;
    }

    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    TWeakObjectPtr<UAsyncAction_LoadGameAssetsBatch> WeakThis(this);
    TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestAsyncLoad(
        MoveTemp(TargetPaths),
        FStreamableDelegate::CreateLambda([WeakThis]() {
            if (UAsyncAction_LoadGameAssetsBatch* StrongThis = WeakThis.Get()) {
                StrongThis->HandleLoadCompleted();
            }
        }),
        ToAsyncLoadPriority(LoadPriority)
    );

    if (!RawHandle.IsValid()) {
        OnFailure.Broadcast();
        OnFailureNative.Broadcast();
        SetReadyToDestroy();
        return;
    }

    RawHandle->BindUpdateDelegate(FStreamableUpdateDelegate::CreateLambda(
        [WeakThis](TSharedRef<FStreamableHandle> InHandle) {
            if (UAsyncAction_LoadGameAssetsBatch* StrongThis = WeakThis.Get()) {
                if (StrongThis->WorldContext.IsValid()) {
                    const float CurrentProgress = InHandle->GetProgress();
                    StrongThis->OnProgress.Broadcast(CurrentProgress);
                    StrongThis->OnProgressNative.Broadcast(CurrentProgress);
                }
            }
        }
    ));

    ActiveHandle = FGameAssetLoadHandle(MoveTemp(RawHandle));
}

void UAsyncAction_LoadGameAssetsBatch::HandleLoadCompleted() {
    if (!WorldContext.IsValid()) {
        SetReadyToDestroy();
        return;
    }

    TArray<UObject*> LoadedAssets;
    LoadedAssets.Reserve(TargetAssets.Num());
    bool bAllSucceeded = !ActiveHandle.HasError();

    if (bAllSucceeded) {
        for (const TSoftObjectPtr<UObject>& Asset : TargetAssets) {
            UObject* Loaded = Asset.Get();
            if (!Loaded) {
                Loaded = Asset.ToSoftObjectPath().ResolveObject();
            }
            if (Loaded != nullptr) {
                LoadedAssets.Add(Loaded);
            } else {
                bAllSucceeded = false;
                break;
            }
        }
    }

    if (bAllSucceeded) {
        OnProgress.Broadcast(1.0f);
        OnProgressNative.Broadcast(1.0f);
        OnSuccess.Broadcast(LoadedAssets);
        OnSuccessNative.Broadcast(LoadedAssets);
    } else {
        OnFailure.Broadcast();
        OnFailureNative.Broadcast();
    }
    SetReadyToDestroy();
}

void UAsyncAction_LoadGameAssetsBatch::Cancel() {
    ActiveHandle.Cancel();
    SetReadyToDestroy();
}

void UAsyncAction_LoadGameAssetsBatch::SetReadyToDestroy() {
    ActiveHandle.Cancel();
    Super::SetReadyToDestroy();
}
