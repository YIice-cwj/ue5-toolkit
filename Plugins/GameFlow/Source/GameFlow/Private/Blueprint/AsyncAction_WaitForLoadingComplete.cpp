/**
 * @file AsyncAction_WaitForLoadingComplete.cpp
 * @brief UAsyncAction_WaitForLoadingComplete 蓝图异步动作节点实现
 */

#include "Blueprint/AsyncAction_WaitForLoadingComplete.h"

#include "Engine/GameInstance.h"
#include "System/ULoadingSubsystem.h"

UAsyncAction_WaitForLoadingComplete* UAsyncAction_WaitForLoadingComplete::WaitForLoadingComplete(
    const UObject* WorldContextObject
) {
    UAsyncAction_WaitForLoadingComplete* Action = NewObject<UAsyncAction_WaitForLoadingComplete>();
    if (ULoadingSubsystem* Subsystem = ULoadingSubsystem::Get(WorldContextObject)) {
        Action->SubsystemWeak = Subsystem;
        Action->RegisterWithGameInstance(Subsystem->GetGameInstance());
    }
    return Action;
}

void UAsyncAction_WaitForLoadingComplete::Activate() {
    if (bActivated || bTerminated) {
        return;
    }
    bActivated = true;

    ULoadingSubsystem* Subsystem = SubsystemWeak.Get();
    if (Subsystem == nullptr) {
        FinishCancelled();
        return;
    }

    if (!Subsystem->IsLoading()) {
        FinishCompleted();
        return;
    }

    TWeakObjectPtr<UAsyncAction_WaitForLoadingComplete> WeakThis(this);
    StateListenerHandle = Subsystem->OnLoadingStateChangedNative.AddLambda(
        [WeakThis](bool bIsLoading, FName LevelName) {
            if (UAsyncAction_WaitForLoadingComplete* StrongThis = WeakThis.Get()) {
                StrongThis->HandleLoadingStateChanged(bIsLoading, LevelName);
            }
        }
    );
    AbortListenerHandle = Subsystem->OnLoadingAbortedNative.AddLambda(
        [WeakThis]() {
            if (UAsyncAction_WaitForLoadingComplete* StrongThis = WeakThis.Get()) {
                StrongThis->HandleLoadingAborted();
            }
        }
    );
}

void UAsyncAction_WaitForLoadingComplete::SetReadyToDestroy() {
    bTerminated = true;
    UnbindSubsystemDelegates();
    Super::SetReadyToDestroy();
}

void UAsyncAction_WaitForLoadingComplete::BeginDestroy() {
    bTerminated = true;
    UnbindSubsystemDelegates();
    Super::BeginDestroy();
}

void UAsyncAction_WaitForLoadingComplete::Cancel() {
    FinishCancelled();
}

void UAsyncAction_WaitForLoadingComplete::HandleLoadingStateChanged(bool bIsLoading, FName) {
    if (!bIsLoading) {
        FinishCompleted();
    }
}

void UAsyncAction_WaitForLoadingComplete::HandleLoadingAborted() {
    FinishCancelled();
}

void UAsyncAction_WaitForLoadingComplete::FinishCompleted() {
    if (bTerminated) {
        return;
    }
    bTerminated = true;
    UnbindSubsystemDelegates();
    OnCompleted.Broadcast();
    Super::SetReadyToDestroy();
}

void UAsyncAction_WaitForLoadingComplete::FinishCancelled() {
    if (bTerminated) {
        return;
    }
    bTerminated = true;
    UnbindSubsystemDelegates();
    OnCancelled.Broadcast();
    Super::SetReadyToDestroy();
}

void UAsyncAction_WaitForLoadingComplete::UnbindSubsystemDelegates() {
    if (ULoadingSubsystem* Subsystem = SubsystemWeak.Get()) {
        if (StateListenerHandle.IsValid()) {
            Subsystem->OnLoadingStateChangedNative.Remove(StateListenerHandle);
        }
        if (AbortListenerHandle.IsValid()) {
            Subsystem->OnLoadingAbortedNative.Remove(AbortListenerHandle);
        }
    }
    StateListenerHandle.Reset();
    AbortListenerHandle.Reset();
}
