// Copyright Epic Games, Inc. All Rights Reserved.

#include "Message/AsyncAction_ListenForGameplayMessage.h"
#include "Message/GameplayMessageSubsystem.h"

UAsyncAction_ListenForGameplayMessage* UAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages(
    UObject* WorldContextObject,
    FGameplayTag Channel,
    EGameplayMessageMatch MatchRule
) {
    UAsyncAction_ListenForGameplayMessage* Action = NewObject<UAsyncAction_ListenForGameplayMessage>();
    Action->WorldContextObjectWeak = WorldContextObject;
    Action->TargetChannel = Channel;
    Action->TargetMatchRule = MatchRule;
    Action->RegisterWithGameInstance(WorldContextObject);
    return Action;
}

void UAsyncAction_ListenForGameplayMessage::Activate() {
    if (!TargetChannel.IsValid()) {
        SetReadyToDestroy();
        return;
    }

    if (UGameplayMessageSubsystem* Subsystem = UGameplayMessageSubsystem::GetPtr(WorldContextObjectWeak.Get())) {
        ListenerHandle = Subsystem->RegisterListenerInternal(
            TargetChannel,
            nullptr,
            [WeakThis = TWeakObjectPtr<UAsyncAction_ListenForGameplayMessage>(this)](FGameplayTag InChannel, const UScriptStruct*, const void*) {
                if (UAsyncAction_ListenForGameplayMessage* StrongThis = WeakThis.Get()) {
                    StrongThis->OnMessageReceived.Broadcast(InChannel);
                }
            },
            TargetMatchRule
        );
    } else {
        SetReadyToDestroy();
    }
}

void UAsyncAction_ListenForGameplayMessage::SetReadyToDestroy() {
    ListenerHandle.Unregister();
    Super::SetReadyToDestroy();
}

