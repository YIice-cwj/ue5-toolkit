/**
 * @file AsyncAction_LoadGameAsset.cpp
 * @brief UAsyncAction_LoadGameAsset 软引用资产异步流式加载蓝图动作节点实现
 */

#include "Asset/AsyncAction_LoadGameAsset.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

UAsyncAction_LoadGameAsset* UAsyncAction_LoadGameAsset::LoadGameAssetAsync(
    UObject* WorldContextObject,
    TSoftObjectPtr<UObject> Asset,
    EGameAssetPriority Priority
) {
    UAsyncAction_LoadGameAsset* Action = NewObject<UAsyncAction_LoadGameAsset>();
    Action->WorldContext = WorldContextObject;
    Action->TargetAsset = Asset;
    Action->LoadPriority = Priority;
    Action->RegisterWithGameInstance(WorldContextObject);
    return Action;
}

UAsyncAction_LoadGameAsset* UAsyncAction_LoadGameAsset::LoadGameClassAsync(
    UObject* WorldContextObject,
    TSoftClassPtr<UObject> Class,
    EGameAssetPriority Priority
) {
    UAsyncAction_LoadGameAsset* Action = NewObject<UAsyncAction_LoadGameAsset>();
    Action->WorldContext = WorldContextObject;
    Action->TargetAsset = TSoftObjectPtr<UObject>(Class.ToSoftObjectPath());
    Action->LoadPriority = Priority;
    Action->RegisterWithGameInstance(WorldContextObject);
    return Action;
}

void UAsyncAction_LoadGameAsset::Activate() {
    if (!WorldContext.IsValid() || TargetAsset.IsNull()) {
        OnFailure.Broadcast();
        OnFailureNative.Broadcast();
        SetReadyToDestroy();
        return;
    }

    UObject* Resident = TargetAsset.Get();
    if (!Resident) {
        Resident = TargetAsset.ToSoftObjectPath().ResolveObject();
    }
    if (Resident != nullptr) {
        OnProgress.Broadcast(1.0f);
        OnProgressNative.Broadcast(1.0f);
        OnSuccess.Broadcast(Resident);
        OnSuccessNative.Broadcast(Resident);
        SetReadyToDestroy();
        return;
    }

    const FSoftObjectPath TargetPath = TargetAsset.ToSoftObjectPath();
    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();

    TWeakObjectPtr<UAsyncAction_LoadGameAsset> WeakThis(this);
    TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestAsyncLoad(
        TargetPath,
        FStreamableDelegate::CreateLambda([WeakThis]() {
            if (UAsyncAction_LoadGameAsset* StrongThis = WeakThis.Get()) {
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
            if (UAsyncAction_LoadGameAsset* StrongThis = WeakThis.Get()) {
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

void UAsyncAction_LoadGameAsset::HandleLoadCompleted() {
    if (!WorldContext.IsValid()) {
        SetReadyToDestroy();
        return;
    }

    UObject* Loaded = TargetAsset.ToSoftObjectPath().ResolveObject();
    if (Loaded != nullptr) {
        OnProgress.Broadcast(1.0f);
        OnProgressNative.Broadcast(1.0f);
        OnSuccess.Broadcast(Loaded);
        OnSuccessNative.Broadcast(Loaded);
    } else {
        OnFailure.Broadcast();
        OnFailureNative.Broadcast();
    }
    SetReadyToDestroy();
}

void UAsyncAction_LoadGameAsset::Cancel() {
    ActiveHandle.Cancel();
    SetReadyToDestroy();
}

void UAsyncAction_LoadGameAsset::SetReadyToDestroy() {
    ActiveHandle.Cancel();
    Super::SetReadyToDestroy();
}
