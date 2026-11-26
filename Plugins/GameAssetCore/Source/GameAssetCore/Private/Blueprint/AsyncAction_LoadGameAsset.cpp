/**
 * @file AsyncAction_LoadGameAsset.cpp
 * @brief UAsyncAction_LoadGameAsset 软引用资产异步流式加载蓝图动作节点实现
 */

#include "Blueprint/AsyncAction_LoadGameAsset.h"

#include "Engine/GameInstance.h"
#include "Loader/FGameAssetLoader.h"
#include "UObject/Class.h"

void UAsyncAction_LoadGameAsset::Activate() {
    if (bActivated || bTerminated) {
        return;
    }
    bActivated = true;

    if (!WorldContext.IsValid() || !TargetPath.IsValid()) {
        BroadcastFailureAndDestroy();
        return;
    }

    TWeakObjectPtr<UAsyncAction_LoadGameAsset> WeakThis(this);
    FGameAssetLoadHandle NewHandle = FGameAssetLoader::LoadRawAsync(
        TargetPath,
        this,
        [WeakThis](UObject* LoadedObject) {
            if (UAsyncAction_LoadGameAsset* StrongThis = WeakThis.Get()) {
                StrongThis->HandleAssetLoaded(LoadedObject);
            }
        },
        [WeakThis](float CurrentProgress) {
            if (UAsyncAction_LoadGameAsset* StrongThis = WeakThis.Get()) {
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

void UAsyncAction_LoadGameAsset::SetReadyToDestroy() {
    bTerminated = true;
    ReleaseOrCancelHandle();
    Super::SetReadyToDestroy();
}

void UAsyncAction_LoadGameAsset::BeginDestroy() {
    bTerminated = true;
    ReleaseOrCancelHandle();
    Super::BeginDestroy();
}

void UAsyncAction_LoadGameAsset::Cancel() {
    if (bTerminated) {
        return;
    }
    bTerminated = true;
    ActiveHandle.Cancel();
    Super::SetReadyToDestroy();
}

UAsyncAction_LoadGameAsset* UAsyncAction_LoadGameAsset::LoadGameAssetAsync(
    UObject* WorldContextObject,
    TSoftObjectPtr<UObject> Asset,
    EGameAssetPriority Priority
) {
    UAsyncAction_LoadGameAsset* Action = NewObject<UAsyncAction_LoadGameAsset>();
    Action->WorldContext = WorldContextObject;
    Action->TargetPath = Asset.ToSoftObjectPath();
    Action->LoadPriority = Priority;
    Action->bRequireClass = false;
    Action->BindToGameInstance(WorldContextObject);
    return Action;
}

UAsyncAction_LoadGameAsset* UAsyncAction_LoadGameAsset::LoadGameClassAsync(
    UObject* WorldContextObject,
    TSoftClassPtr<UObject> Class,
    EGameAssetPriority Priority
) {
    UAsyncAction_LoadGameAsset* Action = NewObject<UAsyncAction_LoadGameAsset>();
    Action->WorldContext = WorldContextObject;
    Action->TargetPath = Class.ToSoftObjectPath();
    Action->LoadPriority = Priority;
    Action->bRequireClass = true;
    Action->BindToGameInstance(WorldContextObject);
    return Action;
}

void UAsyncAction_LoadGameAsset::BindToGameInstance(UObject* WorldContextObject) {
    if (UGameInstance* GameInstance = Cast<UGameInstance>(WorldContextObject)) {
        RegisterWithGameInstance(GameInstance);
    } else if (WorldContextObject != nullptr) {
        RegisterWithGameInstance(WorldContextObject);
    }
}

void UAsyncAction_LoadGameAsset::HandleAssetLoaded(UObject* LoadedObject) {
    if (bTerminated || !WorldContext.IsValid()) {
        SetReadyToDestroy();
        return;
    }

    UObject* ValidObject = ::GetValid(LoadedObject);
    if (ValidObject == nullptr) {
        BroadcastFailureAndDestroy();
        return;
    }

    if (bRequireClass && !ValidObject->IsA<UClass>()) {
        FGameAssetLoader::LogAssetTypeMismatch(TargetPath, ValidObject->GetClass(), UClass::StaticClass());
        BroadcastFailureAndDestroy();
        return;
    }

    OnSuccess.Broadcast(ValidObject);
    SetReadyToDestroy();
}

void UAsyncAction_LoadGameAsset::HandleProgressUpdated(float CurrentProgress) {
    if (bTerminated || !WorldContext.IsValid()) {
        return;
    }
    OnProgress.Broadcast(CurrentProgress);
}

void UAsyncAction_LoadGameAsset::BroadcastFailureAndDestroy() {
    if (!bTerminated && WorldContext.IsValid()) {
        OnFailure.Broadcast();
    }
    SetReadyToDestroy();
}

void UAsyncAction_LoadGameAsset::ReleaseOrCancelHandle() {
    if (ActiveHandle.IsLoading()) {
        ActiveHandle.Cancel();
    } else {
        ActiveHandle.Reset();
    }
}
