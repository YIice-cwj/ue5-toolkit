/**
 * @file FGameAssetLoader.cpp
 * @brief FGameAssetLoader 非模板底层流送调度、事件驱动协程等待器、路径去重与结构化日志实现
 */

#include "Loader/FGameAssetLoader.h"

#include <atomic>

#include "Async/FGameThreadTaskRunner.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "HAL/CriticalSection.h"
#include "Loader/FAsyncRequestCancelBridge.h"
#include "Logging/GameLogCategories.h"
#include "Misc/ScopeLock.h"

using Game::Asset::Private::FAsyncRequestCancelBridge;

struct FGameAssetLoader::FSharedCoroLoadState {
    FCriticalSection Lock;
    TSharedPtr<FStreamableHandle> Handle;
    FCancellationTokenRegistration CancelRegistration;
    std::atomic<bool> bFinished{false};
    bool bClosed = false;
    bool bAbortedBeforeBind = false;

    void AttachHandle(const TSharedPtr<FStreamableHandle>& InHandle) {
        TSharedPtr<FStreamableHandle> HandleToCancel;
        {
            FScopeLock ScopeLock(&Lock);
            if (bAbortedBeforeBind) {
                HandleToCancel = InHandle;
            } else {
                Handle = InHandle;
            }
        }
        FAsyncRequestCancelBridge::CancelHandleOnGameThread(MoveTemp(HandleToCancel));
    }

    void AttachCancelRegistration(FCancellationTokenRegistration InRegistration) {
        FCancellationTokenRegistration ToUnregister;
        {
            FScopeLock ScopeLock(&Lock);
            if (bClosed) {
                ToUnregister = MoveTemp(InRegistration);
            } else {
                CancelRegistration = MoveTemp(InRegistration);
            }
        }
        ToUnregister.Unregister();
    }

    void FinishAndUnregister() {
        FCancellationTokenRegistration LocalRegistration;
        {
            FScopeLock ScopeLock(&Lock);
            bClosed = true;
            LocalRegistration = MoveTemp(CancelRegistration);
        }
        LocalRegistration.Unregister();
    }

    void CancelActiveHandle() {
        TSharedPtr<FStreamableHandle> LocalHandle;
        FCancellationTokenRegistration LocalRegistration;
        {
            FScopeLock ScopeLock(&Lock);
            bClosed = true;
            bAbortedBeforeBind = true;
            LocalHandle = MoveTemp(Handle);
            LocalRegistration = MoveTemp(CancelRegistration);
        }
        LocalRegistration.Unregister();
        FAsyncRequestCancelBridge::CancelHandleOnGameThread(MoveTemp(LocalHandle));
    }

    TSharedPtr<FStreamableHandle> TakeHandle() {
        FScopeLock ScopeLock(&Lock);
        return MoveTemp(Handle);
    }
};

namespace {

constexpr bool bUnmanagedStreamableHandle = false;
constexpr bool bStartStreamableStalled = true;

template <typename TSharedCoroState, typename TRequestFn>
void BindCoroStreamableRequest(
    TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard,
    TSharedRef<TSharedCoroState, ESPMode::ThreadSafe> LocalState,
    const FCancellationToken& Token,
    TRequestFn&& StartRequest
) {
    InGuard->GetState()->AddCompletionCallback([LocalState](EGameCoroStatus FinalStatus) {
        if (FinalStatus == EGameCoroStatus::Cancelled) {
            LocalState->bFinished.store(true, std::memory_order_release);
            LocalState->CancelActiveHandle();
        }
    });

    TSharedPtr<FStreamableHandle> LocalHandle = StartRequest(
        FStreamableDelegate::CreateLambda([InGuard, LocalState]() {
            if (LocalState->bFinished.exchange(true, std::memory_order_acq_rel)) {
                return;
            }
            LocalState->FinishAndUnregister();
            InGuard->ResumeOrAbort();
        })
    );

    if (!LocalHandle.IsValid()) {
        if (!LocalState->bFinished.exchange(true, std::memory_order_acq_rel)) {
            LocalState->FinishAndUnregister();
            InGuard->ResumeOrAbort();
        }
        return;
    }

    LocalState->AttachHandle(LocalHandle);
    if (!LocalState->bFinished.load(std::memory_order_acquire) && Token.CanBeCancelled()) {
        LocalState->AttachCancelRegistration(Token.Register([InGuard, LocalState]() {
            if (LocalState->bFinished.exchange(true, std::memory_order_acq_rel)) {
                return;
            }
            LocalState->CancelActiveHandle();
            if (IsInGameThread()) {
                InGuard->AbortAndDestroyFrame();
            } else {
                FGameThreadTaskRunner::RunOnGameThread([InGuard]() {
                    InGuard->AbortAndDestroyFrame();
                });
            }
        }));
    }

    if (!LocalState->bFinished.load(std::memory_order_acquire)) {
        LocalHandle->StartStalledHandle();
    }
}

void BindAsyncHandleDelegates(
    const TSharedPtr<FStreamableHandle>& RawHandle,
    const TSharedRef<FAsyncRequestCancelBridge, ESPMode::ThreadSafe>& CancelBridge,
    const TWeakObjectPtr<UObject>& Owner,
    const TFunction<void(float Progress)>& OnProgress,
    const FCancellationToken& CancellationToken
) {
    CancelBridge->AttachHandle(RawHandle);

    if (!CancelBridge->bFinished.load(std::memory_order_acquire)) {
        RawHandle->BindCancelDelegate(FStreamableDelegate::CreateLambda([CancelBridge]() {
            CancelBridge->NotifyHandleCanceled();
        }));

        if (OnProgress) {
            RawHandle->BindUpdateDelegate(FStreamableUpdateDelegate::CreateLambda(
                [Owner, OnProgress, CancellationToken](TSharedRef<FStreamableHandle> InHandle) {
                    if (!Owner.IsValid() || CancellationToken.IsCancellationRequested()) {
                        return;
                    }
                    OnProgress(InHandle->GetProgress());
                }
            ));
        }

        if (CancellationToken.CanBeCancelled()) {
            CancelBridge->AttachTokenRegistration(CancellationToken.Register([CancelBridge]() {
                CancelBridge->CancelFromToken();
            }));
        }
    }

    if (!CancelBridge->bFinished.load(std::memory_order_acquire)) {
        RawHandle->StartStalledHandle();
    }
}

}  // namespace

FGameAssetLoader::FStreamableAssetAwaiter::FStreamableAssetAwaiter(
    FSoftObjectPath InPath,
    FCancellationToken InToken,
    EGameAssetPriority InPriority
)
    : TargetPath(MoveTemp(InPath))
    , Token(MoveTemp(InToken))
    , Priority(InPriority)
    , SharedState(MakeShared<FSharedCoroLoadState, ESPMode::ThreadSafe>()) {
}

FGameAssetLoader::FStreamableAssetAwaiter::FStreamableAssetAwaiter(FStreamableAssetAwaiter&& Other) noexcept = default;

FGameAssetLoader::FStreamableAssetAwaiter::~FStreamableAssetAwaiter() = default;

bool FGameAssetLoader::FStreamableAssetAwaiter::await_ready() {
    if (!TargetPath.IsValid()) {
        return true;
    }
    PreResolvedObject = ResolveResidentObject(TargetPath);
    return PreResolvedObject != nullptr;
}

void FGameAssetLoader::FStreamableAssetAwaiter::SuspendWithGuard(
    TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard
) {
    if (!UAssetManager::IsInitialized()) {
        GAME_LOG(Warning, TEXT("【资产加载】UAssetManager 尚未初始化，无法执行协程流式加载: %s"), *TargetPath.ToString());
        InGuard->ResumeOrAbort();
        return;
    }

    GAME_LOG(Verbose, TEXT("【资产加载】发起协程单资产异步流送: %s (Priority=%d)"), *TargetPath.ToString(), static_cast<int32>(Priority));

    BindCoroStreamableRequest(
        MoveTemp(InGuard),
        SharedState.ToSharedRef(),
        Token,
        [this](FStreamableDelegate OnComplete) {
            FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
            return Streamable.RequestAsyncLoad(
                TargetPath,
                MoveTemp(OnComplete),
                ToAsyncLoadPriority(Priority),
                bUnmanagedStreamableHandle,
                bStartStreamableStalled
            );
        }
    );
}

UObject* FGameAssetLoader::FStreamableAssetAwaiter::await_resume() {
    if (PreResolvedObject != nullptr) {
        return PreResolvedObject;
    }

    UObject* Loaded = nullptr;
    TSharedPtr<FStreamableHandle> CompletedHandle;
    if (SharedState.IsValid()) {
        CompletedHandle = SharedState->TakeHandle();
        if (CompletedHandle.IsValid() && CompletedHandle->HasLoadCompleted()) {
            Loaded = ::GetValid(CompletedHandle->GetLoadedAsset());
        }
    }
    if (Loaded == nullptr) {
        Loaded = ResolveResidentObject(TargetPath);
    }
    if (Loaded == nullptr && TargetPath.IsValid()) {
        GAME_LOG(Warning, TEXT("【资产加载】协程异步流式加载失败，资产路径无法解析: %s"), *TargetPath.ToString());
    }
    return Loaded;
}

FGameAssetLoader::FStreamableBatchAwaiter::FStreamableBatchAwaiter(
    TArray<FSoftObjectPath> InUniquePaths,
    FCancellationToken InToken,
    EGameAssetPriority InPriority
)
    : UniquePaths(MoveTemp(InUniquePaths))
    , Token(MoveTemp(InToken))
    , Priority(InPriority)
    , SharedState(MakeShared<FSharedCoroLoadState, ESPMode::ThreadSafe>()) {
}

FGameAssetLoader::FStreamableBatchAwaiter::FStreamableBatchAwaiter(FStreamableBatchAwaiter&& Other) noexcept = default;

FGameAssetLoader::FStreamableBatchAwaiter::~FStreamableBatchAwaiter() = default;

bool FGameAssetLoader::FStreamableBatchAwaiter::await_ready() const noexcept {
    return UniquePaths.IsEmpty();
}

void FGameAssetLoader::FStreamableBatchAwaiter::SuspendWithGuard(
    TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard
) {
    if (!UAssetManager::IsInitialized()) {
        GAME_LOG(Warning, TEXT("【资产加载】UAssetManager 尚未初始化，无法执行批量协程流式加载（请求数=%d）"), UniquePaths.Num());
        InGuard->ResumeOrAbort();
        return;
    }

    const int32 PathCount = UniquePaths.Num();
    GAME_LOG(Verbose, TEXT("【资产加载】发起协程批量资产异步流送: 去重路径数=%d, Priority=%d"), PathCount, static_cast<int32>(Priority));

    BindCoroStreamableRequest(
        MoveTemp(InGuard),
        SharedState.ToSharedRef(),
        Token,
        [this](FStreamableDelegate OnComplete) {
            FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
            return Streamable.RequestAsyncLoad(
                MoveTemp(UniquePaths),
                MoveTemp(OnComplete),
                ToAsyncLoadPriority(Priority),
                bUnmanagedStreamableHandle,
                bStartStreamableStalled
            );
        }
    );
}

FGameAssetLoadHandle FGameAssetLoader::FStreamableBatchAwaiter::await_resume() {
    if (!SharedState.IsValid()) {
        return FGameAssetLoadHandle();
    }
    TSharedPtr<FStreamableHandle> CompletedHandle = SharedState->TakeHandle();
    if (CompletedHandle.IsValid() && CompletedHandle->HasError()) {
        GAME_LOG(Warning, TEXT("【资产加载】协程批量流式加载过程中存在底层流送错误。"));
    }
    return FGameAssetLoadHandle(MoveTemp(CompletedHandle));
}

FGameAssetLoadHandle FGameAssetLoader::LoadBatchAsync(
    const TArray<FSoftObjectPath>& AssetPaths,
    TWeakObjectPtr<UObject> Owner,
    TFunction<void()> OnAllLoaded,
    TFunction<void(float Progress)> OnProgress,
    const FCancellationToken& CancellationToken,
    EGameAssetPriority Priority
) {
    return LoadRawBatchAsync(AssetPaths, Owner, MoveTemp(OnAllLoaded), MoveTemp(OnProgress), CancellationToken, Priority);
}

FGameAssetLoadHandle FGameAssetLoader::LoadRawAsync(
    const FSoftObjectPath& TargetPath,
    TWeakObjectPtr<UObject> Owner,
    TFunction<void(UObject*)> OnLoaded,
    TFunction<void(float Progress)> OnProgress,
    const FCancellationToken& CancellationToken,
    EGameAssetPriority Priority
) {
    if (!Owner.IsValid() || !OnLoaded || !TargetPath.IsValid() || CancellationToken.IsCancellationRequested()) {
        return FGameAssetLoadHandle();
    }

    if (UObject* Resident = ResolveResidentObject(TargetPath)) {
        if (OnProgress) {
            OnProgress(1.0f);
        }
        OnLoaded(Resident);
        return FGameAssetLoadHandle();
    }

    if (!UAssetManager::IsInitialized()) {
        GAME_LOG(Warning, TEXT("【资产加载】UAssetManager 尚未初始化，无法异步加载资产: %s"), *TargetPath.ToString());
        OnLoaded(nullptr);
        return FGameAssetLoadHandle();
    }

    TSharedRef<FAsyncRequestCancelBridge, ESPMode::ThreadSafe> CancelBridge =
        MakeShared<FAsyncRequestCancelBridge, ESPMode::ThreadSafe>();

    GAME_LOG(Verbose, TEXT("【资产加载】发起异步单资产加载: %s (Priority=%d)"), *TargetPath.ToString(), static_cast<int32>(Priority));

    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestAsyncLoad(
        TargetPath,
        FStreamableDelegate::CreateLambda(
            [Owner, TargetPath, Callback = MoveTemp(OnLoaded), OnProgress, CancellationToken, CancelBridge]() mutable {
                if (!CancelBridge->TryFinishFromStreamable()) {
                    return;
                }
                if (!Owner.IsValid() || CancellationToken.IsCancellationRequested()) {
                    return;
                }

                UObject* Loaded = ResolveResidentObject(TargetPath);
                if (Loaded != nullptr && OnProgress) {
                    OnProgress(1.0f);
                }
                if (Loaded == nullptr) {
                    GAME_LOG(Warning, TEXT("【资产加载】异步加载失败，资产路径不存在或无法解析: %s"), *TargetPath.ToString());
                }
                Callback(Loaded);
            }
        ),
        ToAsyncLoadPriority(Priority),
        bUnmanagedStreamableHandle,
        bStartStreamableStalled
    );

    if (!RawHandle.IsValid()) {
        GAME_LOG(Warning, TEXT("【资产加载】创建异步流式句柄失败: %s"), *TargetPath.ToString());
        return FGameAssetLoadHandle();
    }

    BindAsyncHandleDelegates(RawHandle, CancelBridge, Owner, OnProgress, CancellationToken);
    return FGameAssetLoadHandle(MoveTemp(RawHandle));
}

FGameAssetLoadHandle FGameAssetLoader::LoadRawBatchAsync(
    TConstArrayView<FSoftObjectPath> AssetPaths,
    TWeakObjectPtr<UObject> Owner,
    TFunction<void()> OnAllLoaded,
    TFunction<void(float Progress)> OnProgress,
    const FCancellationToken& CancellationToken,
    EGameAssetPriority Priority
) {
    if (!Owner.IsValid() || !OnAllLoaded || CancellationToken.IsCancellationRequested()) {
        return FGameAssetLoadHandle();
    }

    bool bAllResident = true;
    for (const FSoftObjectPath& Path : AssetPaths) {
        if (Path.IsValid() && ResolveResidentObject(Path) == nullptr) {
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

    if (!UAssetManager::IsInitialized()) {
        GAME_LOG(Warning, TEXT("【资产加载】UAssetManager 尚未初始化，无法执行批量异步流送。"));
        return FGameAssetLoadHandle();
    }

    TArray<FSoftObjectPath> UniqueValidPaths = CollectUniqueValidPaths(AssetPaths);
    const int32 UniqueCount = UniqueValidPaths.Num();
    GAME_LOG(Verbose, TEXT("【资产加载】发起批量异步加载: 去重有效路径数=%d, Priority=%d"), UniqueCount, static_cast<int32>(Priority));

    TSharedRef<FAsyncRequestCancelBridge, ESPMode::ThreadSafe> CancelBridge =
        MakeShared<FAsyncRequestCancelBridge, ESPMode::ThreadSafe>();

    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestAsyncLoad(
        MoveTemp(UniqueValidPaths),
        FStreamableDelegate::CreateLambda(
            [Owner, Callback = MoveTemp(OnAllLoaded), OnProgress, CancellationToken, CancelBridge, UniqueCount]() mutable {
                if (!CancelBridge->TryFinishFromStreamable()) {
                    return;
                }
                if (!Owner.IsValid() || CancellationToken.IsCancellationRequested()) {
                    return;
                }
                if (OnProgress && CancelBridge->AreAllLoadedAssetsValid(UniqueCount)) {
                    OnProgress(1.0f);
                }
                Callback();
            }
        ),
        ToAsyncLoadPriority(Priority),
        bUnmanagedStreamableHandle,
        bStartStreamableStalled
    );

    if (!RawHandle.IsValid()) {
        GAME_LOG(Warning, TEXT("【资产加载】创建批量异步流式句柄失败（去重路径数=%d）"), UniqueCount);
        return FGameAssetLoadHandle();
    }

    BindAsyncHandleDelegates(RawHandle, CancelBridge, Owner, OnProgress, CancellationToken);
    return FGameAssetLoadHandle(MoveTemp(RawHandle));
}

TGameStatusResult<UObject*> FGameAssetLoader::LoadRawSync(const FSoftObjectPath& TargetPath) {
    if (!TargetPath.IsValid()) {
        return TGameStatusResult<UObject*>::Failure(
            Game::ModuleId::Asset,
            Game::AssetErrorCodes::InvalidAssetPath,
            TEXT("TargetPath is null or invalid.")
        );
    }

    if (UObject* Resident = ResolveResidentObject(TargetPath)) {
        return TGameStatusResult<UObject*>::Success(Resident);
    }

    if (!IsInGameThread()) {
        const FString PathText = TargetPath.ToString();
        GAME_LOG(Warning, TEXT("【资产加载】拒绝在非游戏线程对未驻留资产执行同步加载: %s"), *PathText);
        return TGameStatusResult<UObject*>::Failure(
            Game::ModuleId::Asset,
            Game::AssetErrorCodes::InvalidThread,
            FString::Printf(TEXT("Cannot synchronously load unresident asset '%s' off the GameThread."), *PathText)
        );
    }

    UObject* Loaded = nullptr;
    if (UAssetManager::IsInitialized()) {
        FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
        const TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestSyncLoad(TargetPath);
        if (RawHandle.IsValid() && !RawHandle->HasError()) {
            Loaded = ::GetValid(RawHandle->GetLoadedAsset());
        }
    }
    if (Loaded == nullptr) {
        Loaded = ResolveResidentObject(TargetPath);
    }

    if (Loaded != nullptr) {
        return TGameStatusResult<UObject*>::Success(Loaded);
    }

    const FString PathText = TargetPath.ToString();
    GAME_LOG(Warning, TEXT("【资产加载】同步阻塞加载失败，目标路径不存在或包体损坏: %s"), *PathText);
    return TGameStatusResult<UObject*>::Failure(
        Game::ModuleId::Asset,
        Game::AssetErrorCodes::LoadFailed,
        FString::Printf(TEXT("Failed to synchronously load asset '%s'."), *PathText)
    );
}

UObject* FGameAssetLoader::ResolveResidentObject(const FSoftObjectPath& TargetPath) {
    if (!TargetPath.IsValid()) {
        return nullptr;
    }
    return ::GetValid(TargetPath.ResolveObject());
}

TArray<FSoftObjectPath> FGameAssetLoader::CollectUniqueValidPaths(TConstArrayView<FSoftObjectPath> InPaths) {
    TArray<FSoftObjectPath> UniquePaths;
    UniquePaths.Reserve(InPaths.Num());
    TSet<FSoftObjectPath> SeenPaths;
    SeenPaths.Reserve(InPaths.Num());

    for (const FSoftObjectPath& Path : InPaths) {
        if (!Path.IsValid()) {
            continue;
        }
        bool bAlreadyInSet = false;
        SeenPaths.Add(Path, &bAlreadyInSet);
        if (!bAlreadyInSet) {
            UniquePaths.Add(Path);
        }
    }
    return UniquePaths;
}

void FGameAssetLoader::LogAssetTypeMismatch(
    const FSoftObjectPath& TargetPath,
    const UClass* ActualClass,
    const UClass* ExpectedClass
) {
    GAME_LOG(
        Warning,
        TEXT("【资产加载】资产类型契约不匹配: 路径=[%s], 实际类型=[%s], 期望类型=[%s]"),
        *TargetPath.ToString(),
        ActualClass ? *ActualClass->GetName() : TEXT("None"),
        ExpectedClass ? *ExpectedClass->GetName() : TEXT("None")
    );
}
