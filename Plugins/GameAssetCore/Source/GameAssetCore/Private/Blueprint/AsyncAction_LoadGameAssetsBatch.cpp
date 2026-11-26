/**
 * @file AsyncAction_LoadGameAssetsBatch.cpp
 * @brief UAsyncAction_LoadGameAssetsBatch 软引用资产批量异步流式加载蓝图动作节点实现
 */

#include "Blueprint/AsyncAction_LoadGameAssetsBatch.h"

#include "Engine/GameInstance.h"
#include "Loader/FGameAssetLoader.h"
#include "Logging/GameLogCategories.h"

void UAsyncAction_LoadGameAssetsBatch::Activate() {
    if (bActivated || bTerminated) {
        return;
    }
    bActivated = true;

    if (!WorldContext.IsValid() || TargetAssets.IsEmpty()) {
        BroadcastFailureAndDestroy();
        return;
    }

    TArray<FSoftObjectPath> TargetPaths;
    TargetPaths.Reserve(TargetAssets.Num());
    for (const TSoftObjectPtr<UObject>& Asset : TargetAssets) {
        if (Asset.IsNull()) {
            BroadcastFailureAndDestroy();
            return;
        }
        TargetPaths.Add(Asset.ToSoftObjectPath());
    }

    TWeakObjectPtr<UAsyncAction_LoadGameAssetsBatch> WeakThis(this);
    FGameAssetLoadHandle NewHandle = FGameAssetLoader::LoadRawBatchAsync(
        TargetPaths,
        this,
        [WeakThis]() {
            if (UAsyncAction_LoadGameAssetsBatch* StrongThis = WeakThis.Get()) {
                StrongThis->HandleBatchLoaded();
            }
        },
        [WeakThis](float CurrentProgress) {
            if (UAsyncAction_LoadGameAssetsBatch* StrongThis = WeakThis.Get()) {
                StrongThis->HandleProgressUpdated(CurrentProgress);
            }
        },
        FCancellationToken::None(),
        LoadPriority
    );
    if (bTerminated) {
        if (NewHandle.IsLoading()) {
            NewHandle.Cancel();
        } else {
            NewHandle.Reset();
        }
    } else {
        ActiveHandle = MoveTemp(NewHandle);
    }
}

void UAsyncAction_LoadGameAssetsBatch::SetReadyToDestroy() {
    bTerminated = true;
    ReleaseOrCancelHandle();
    Super::SetReadyToDestroy();
}

void UAsyncAction_LoadGameAssetsBatch::BeginDestroy() {
    bTerminated = true;
    ReleaseOrCancelHandle();
    Super::BeginDestroy();
}

void UAsyncAction_LoadGameAssetsBatch::Cancel() {
    if (bTerminated) {
        return;
    }
    bTerminated = true;
    ActiveHandle.Cancel();
    Super::SetReadyToDestroy();
}

UAsyncAction_LoadGameAssetsBatch* UAsyncAction_LoadGameAssetsBatch::LoadGameAssetsBatchAsync(
    UObject* WorldContextObject,
    const TArray<TSoftObjectPtr<UObject>>& Assets,
    EGameAssetPriority Priority
) {
    UAsyncAction_LoadGameAssetsBatch* Action = NewObject<UAsyncAction_LoadGameAssetsBatch>();
    Action->WorldContext = WorldContextObject;
    Action->TargetAssets = Assets;
    Action->LoadPriority = Priority;
    Action->BindToGameInstance(WorldContextObject);
    return Action;
}

void UAsyncAction_LoadGameAssetsBatch::BindToGameInstance(UObject* WorldContextObject) {
    if (UGameInstance* GameInstance = Cast<UGameInstance>(WorldContextObject)) {
        RegisterWithGameInstance(GameInstance);
    } else if (WorldContextObject != nullptr) {
        RegisterWithGameInstance(WorldContextObject);
    }
}

void UAsyncAction_LoadGameAssetsBatch::HandleBatchLoaded() {
    if (bTerminated || !WorldContext.IsValid()) {
        SetReadyToDestroy();
        return;
    }

    if (ActiveHandle.HasError()) {
        BroadcastFailureAndDestroy();
        return;
    }

    TArray<UObject*> LoadedAssets;
    LoadedAssets.Reserve(TargetAssets.Num());
    for (const TSoftObjectPtr<UObject>& Asset : TargetAssets) {
        UObject* Loaded = Asset.Get();
        if (Loaded == nullptr) {
            GAME_LOG(Warning, TEXT("【资产加载】蓝图批量异步加载未能解析目标资产: %s"), *Asset.ToString());
            BroadcastFailureAndDestroy();
            return;
        }
        LoadedAssets.Add(Loaded);
    }

    OnSuccess.Broadcast(LoadedAssets);
    SetReadyToDestroy();
}

void UAsyncAction_LoadGameAssetsBatch::HandleProgressUpdated(float CurrentProgress) {
    if (bTerminated || !WorldContext.IsValid()) {
        return;
    }
    OnProgress.Broadcast(CurrentProgress);
}

void UAsyncAction_LoadGameAssetsBatch::BroadcastFailureAndDestroy() {
    if (!bTerminated && WorldContext.IsValid()) {
        OnFailure.Broadcast();
    }
    SetReadyToDestroy();
}

void UAsyncAction_LoadGameAssetsBatch::ReleaseOrCancelHandle() {
    if (ActiveHandle.IsLoading()) {
        ActiveHandle.Cancel();
    } else {
        ActiveHandle.Reset();
    }
}
