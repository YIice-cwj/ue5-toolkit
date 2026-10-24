// Copyright Epic Games, Inc. All Rights Reserved.

#include "Message/GameplayMessageSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

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

UGameplayMessageSubsystem& UGameplayMessageSubsystem::Get(const UObject* WorldContextObject) {
    UGameplayMessageSubsystem* Subsystem = GetPtr(WorldContextObject);
    check(Subsystem != nullptr);
    return *Subsystem;
}

UGameplayMessageSubsystem* UGameplayMessageSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (!WorldContextObject) {
        return nullptr;
    }
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)) {
        if (UGameInstance* GameInstance = World->GetGameInstance()) {
            return GameInstance->GetSubsystem<UGameplayMessageSubsystem>();
        }
    }
    return nullptr;
}

void UGameplayMessageSubsystem::Deinitialize() {
    ListenerMap.Empty();
    Super::Deinitialize();
}

bool UGameplayMessageSubsystem::HasListener(FGameplayTag Channel, EGameplayMessageMatch MatchRule) const {
    check(IsInGameThread());
    if (!Channel.IsValid()) {
        return false;
    }

    if (const FChannelListenerList* ExactList = ListenerMap.Find(Channel)) {
        if (!ExactList->Listeners.IsEmpty()) {
            return true;
        }
    }

    if (MatchRule == EGameplayMessageMatch::PartialMatch) {
        const FGameplayTagContainer ParentTags = Channel.GetGameplayTagParents();
        for (const FGameplayTag& ParentTag : ParentTags) {
            if (ParentTag == Channel) {
                continue;
            }
            if (const FChannelListenerList* ParentList = ListenerMap.Find(ParentTag)) {
                for (const FListenerEntry& Entry : ParentList->Listeners) {
                    if (Entry.MatchRule == EGameplayMessageMatch::PartialMatch) {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

void UGameplayMessageSubsystem::UnregisterListener(FGameplayTag Channel, int64 ListenerId) {
    check(IsInGameThread());
    if (ListenerId == 0) {
        return;
    }
    if (FChannelListenerList* List = ListenerMap.Find(Channel)) {
        List->Listeners.RemoveAll([ListenerId](const FListenerEntry& Entry) {
            return Entry.Id == ListenerId;
        });
        if (List->Listeners.IsEmpty()) {
            ListenerMap.Remove(Channel);
        }
    }
}

int32 UGameplayMessageSubsystem::GetListenerCount() const {
    check(IsInGameThread());
    int32 Count = 0;
    for (const auto& Pair : ListenerMap) {
        Count += Pair.Value.Listeners.Num();
    }
    return Count;
}

FGameplayMessageListenerHandle UGameplayMessageSubsystem::RegisterListenerInternal(
    FGameplayTag Channel,
    const UScriptStruct* StructType,
    FRawMessageCallback Callback,
    EGameplayMessageMatch MatchRule
) {
    check(IsInGameThread());
    if (!Channel.IsValid() || !Callback) {
        return FGameplayMessageListenerHandle();
    }

    const int64 ListenerId = ++NextListenerId;
    FChannelListenerList& List = ListenerMap.FindOrAdd(Channel);
    List.Listeners.Add(FListenerEntry{
        ListenerId,
        MatchRule,
        StructType,
        MoveTemp(Callback)
    });

    return FGameplayMessageListenerHandle(this, Channel, ListenerId);
}

void UGameplayMessageSubsystem::BroadcastMessageInternal(
    FGameplayTag Channel,
    const UScriptStruct* StructType,
    const void* Payload
) {
    check(IsInGameThread());
    if (!Channel.IsValid() || !StructType) {
        return;
    }

    TArray<FListenerEntry> ListenersToInvoke;

    auto CheckAndAdd = [&ListenersToInvoke, StructType](const FListenerEntry& Entry) {
        if (const UScriptStruct* ExpectedStruct = Entry.ExpectedStructType.Get()) {
            if (StructType->IsChildOf(ExpectedStruct)) {
                ListenersToInvoke.Add(Entry);
            }
        } else {
            ListenersToInvoke.Add(Entry);
        }
    };

    if (const FChannelListenerList* ExactList = ListenerMap.Find(Channel)) {
        for (const FListenerEntry& Entry : ExactList->Listeners) {
            CheckAndAdd(Entry);
        }
    }

    const FGameplayTagContainer ParentTags = Channel.GetGameplayTagParents();
    for (const FGameplayTag& ParentTag : ParentTags) {
        if (ParentTag == Channel) {
            continue;
        }
        if (const FChannelListenerList* ParentList = ListenerMap.Find(ParentTag)) {
            for (const FListenerEntry& Entry : ParentList->Listeners) {
                if (Entry.MatchRule == EGameplayMessageMatch::PartialMatch) {
                    CheckAndAdd(Entry);
                }
            }
        }
    }

    for (const FListenerEntry& Entry : ListenersToInvoke) {
        Entry.Callback(Channel, StructType, Payload);
    }
}
