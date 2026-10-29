// Copyright Epic Games, Inc. All Rights Reserved.

#include "Message/GameplayMessageTypes.h"
#include "Message/GameplayMessageSubsystem.h"

FGameplayMessageListenerHandle::FListenerRegistration::~FListenerRegistration() {
    Unregister();
}

void FGameplayMessageListenerHandle::FListenerRegistration::Unregister() {
    if (ListenerId != 0) {
        if (UGameplayMessageSubsystem* Subsystem = SubsystemWeak.Get()) {
            Subsystem->UnregisterListener(Channel, ListenerId);
        }
        SubsystemWeak.Reset();
        ListenerId = 0;
        Channel = FGameplayTag();
    }
}

void FGameplayMessageListenerHandle::Unregister() {
    if (Registration.IsValid()) {
        Registration->Unregister();
        Registration.Reset();
    }
}

bool FGameplayMessageListenerHandle::IsValid() const noexcept {
    return Registration.IsValid() && Registration->ListenerId != 0 && Registration->SubsystemWeak.IsValid();
}

FGameplayMessageListenerHandle::FGameplayMessageListenerHandle(UGameplayMessageSubsystem* InSubsystem, FGameplayTag InChannel, int64 InId) {
    Registration = MakeShared<FListenerRegistration>();
    Registration->SubsystemWeak = InSubsystem;
    Registration->Channel = InChannel;
    Registration->ListenerId = InId;
}

