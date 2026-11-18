/**
 * @file AsyncAction_WaitForLoadingComplete.cpp
 * @brief UAsyncAction_WaitForLoadingComplete 蓝图异步动作节点实现
 */

#include "Async/AsyncAction_WaitForLoadingComplete.h"

#include "System/ULoadingSubsystem.h"

UAsyncAction_WaitForLoadingComplete* UAsyncAction_WaitForLoadingComplete::WaitForLoadingComplete(
    const UObject* WorldContextObject
) {
    UAsyncAction_WaitForLoadingComplete* Action = NewObject<UAsyncAction_WaitForLoadingComplete>();
    Action->SubsystemWeak = ULoadingSubsystem::Get(WorldContextObject);
    Action->RegisterWithGameInstance(const_cast<UObject*>(WorldContextObject));
    return Action;
}

void UAsyncAction_WaitForLoadingComplete::Activate() {
    ULoadingSubsystem* Subsystem = SubsystemWeak.Get();
    if (Subsystem == nullptr || !Subsystem->IsLoading()) {
        OnCompleted.Broadcast();
        OnCompletedNative.Broadcast();
        SetReadyToDestroy();
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
}

void UAsyncAction_WaitForLoadingComplete::HandleLoadingStateChanged(bool bIsLoading, FName LevelName) {
    if (!bIsLoading) {
        OnCompleted.Broadcast();
        OnCompletedNative.Broadcast();
        SetReadyToDestroy();
    }
}

void UAsyncAction_WaitForLoadingComplete::Cancel() {
    SetReadyToDestroy();
}

void UAsyncAction_WaitForLoadingComplete::SetReadyToDestroy() {
    if (ULoadingSubsystem* Subsystem = SubsystemWeak.Get()) {
        if (StateListenerHandle.IsValid()) {
            Subsystem->OnLoadingStateChangedNative.Remove(StateListenerHandle);
            StateListenerHandle.Reset();
        }
    }
    OnCompletedNative.Clear();
    Super::SetReadyToDestroy();
}
