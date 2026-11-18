/**
 * @file FGameAssetLoader.cpp
 * @brief FGameAssetLoader 弱引用守卫异步资产加载器批量加载实现
 */

#include "Asset/FGameAssetLoader.h"

FGameAssetLoadHandle FGameAssetLoader::LoadBatchAsync(
    const TArray<FSoftObjectPath>& AssetPaths,
    TWeakObjectPtr<UObject> Owner,
    TFunction<void()> OnAllLoaded,
    TFunction<void(float Progress)> OnProgress,
    const FCancellationToken& CancellationToken,
    EGameAssetPriority Priority
) {
    if (!Owner.IsValid() || !OnAllLoaded || CancellationToken.IsCancellationRequested()) {
        return FGameAssetLoadHandle();
    }

    TArray<FSoftObjectPath> ValidPaths;
    ValidPaths.Reserve(AssetPaths.Num());
    TSet<FSoftObjectPath> SeenPaths;
    SeenPaths.Reserve(AssetPaths.Num());
    for (const FSoftObjectPath& Path : AssetPaths) {
        if (Path.IsValid() && !SeenPaths.Contains(Path)) {
            SeenPaths.Add(Path);
            ValidPaths.Add(Path);
        }
    }

    if (ValidPaths.IsEmpty()) {
        if (OnProgress) {
            OnProgress(1.0f);
        }
        OnAllLoaded();
        return FGameAssetLoadHandle();
    }

    bool bAllResident = true;
    for (const FSoftObjectPath& Path : ValidPaths) {
        if (Path.ResolveObject() == nullptr) {
            bAllResident = false;
            break;
        }
    }

    if (bAllResident) {
        if (OnProgress) {
            OnProgress(1.0f);
        }
        OnAllLoaded();
        return FGameAssetLoadHandle();
    }

    struct FBatchRequestContext {
        FCancellationTokenRegistration TokenRegistration;
    };
    TSharedRef<FBatchRequestContext> RequestContext = MakeShared<FBatchRequestContext>();

    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestAsyncLoad(
        MoveTemp(ValidPaths),
        FStreamableDelegate::CreateLambda([Owner, Callback = MoveTemp(OnAllLoaded), OnProgress, CancellationToken, RequestContext]() mutable {
            if (!Owner.IsValid() || CancellationToken.IsCancellationRequested()) {
                return;
            }
            if (OnProgress) {
                OnProgress(1.0f);
            }
            Callback();
        }),
        ToAsyncLoadPriority(Priority)
    );

    if (RawHandle.IsValid()) {
        if (OnProgress) {
            RawHandle->BindUpdateDelegate(FStreamableUpdateDelegate::CreateLambda(
                [Owner, OnProgress, CancellationToken](TSharedRef<FStreamableHandle> InHandle) {
                    if (Owner.IsValid() && !CancellationToken.IsCancellationRequested()) {
                        OnProgress(InHandle->GetProgress());
                    }
                }
            ));
        }

        if (CancellationToken.CanBeCancelled()) {
            TWeakPtr<FStreamableHandle> WeakHandle = RawHandle;
            RequestContext->TokenRegistration = CancellationToken.Register([WeakHandle]() {
                if (TSharedPtr<FStreamableHandle> Pinned = WeakHandle.Pin()) {
                    Pinned->CancelHandle();
                }
            });
        }
    }

    return FGameAssetLoadHandle(MoveTemp(RawHandle));
}
