// Copyright Epic Games, Inc. All Rights Reserved.

#include "Message/GameplayMessageSubsystem.h"

#include "Async/Async.h"
#include "Config/GameCompileSwitches.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameplayMessage, Log, All);

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

void UGameplayMessageSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    bIsShuttingDown.store(false, std::memory_order_release);
}

void UGameplayMessageSubsystem::Deinitialize() {
    bIsShuttingDown.store(true, std::memory_order_release);
    while (PendingMessageQueue.Dequeue().IsSet()) {
    }
    bDrainScheduled.store(false, std::memory_order_release);

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameplayMessage, Log, TEXT("【消息子系统】子系统注销，清空 %d 个通道的监听器。"), ListenerMap.Num());
    }
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

    if (MatchRule != EGameplayMessageMatch::PartialMatch) {
        return false;
    }

    const FGameplayTagContainer ParentTags = Channel.GetGameplayTagParents();
    for (const FGameplayTag& ParentTag : ParentTags) {
        if (ParentTag == Channel) {
            continue;
        }
        const FChannelListenerList* ParentList = ListenerMap.Find(ParentTag);
        if (ParentList == nullptr) {
            continue;
        }
        for (const FListenerEntry& Entry : ParentList->Listeners) {
            if (Entry.MatchRule == EGameplayMessageMatch::PartialMatch) {
                return true;
            }
        }
    }

    return false;
}

int32 UGameplayMessageSubsystem::FlushPendingMessages() {
    check(IsInGameThread());
    int32 DispatchedCount = 0;

    while (true) {
        bDrainScheduled.store(false, std::memory_order_release);

        bool bDequeuedAny = false;
        while (TOptional<TUniqueFunction<void()>> Task = PendingMessageQueue.Dequeue()) {
            bDequeuedAny = true;
            if (!bIsShuttingDown.load(std::memory_order_acquire) && *Task) {
                (*Task)();
                ++DispatchedCount;
            }
        }

        if (!bDequeuedAny || PendingMessageQueue.IsEmpty()) {
            break;
        }
    }

    return DispatchedCount;
}

void UGameplayMessageSubsystem::UnregisterListener(FGameplayTag Channel, int64 ListenerId) {
    check(IsInGameThread());
    if (ListenerId == 0) {
        return;
    }
    if (FChannelListenerList* List = ListenerMap.Find(Channel)) {
        const int32 RemovedCount = List->Listeners.RemoveAll([ListenerId](const FListenerEntry& Entry) {
            return Entry.Id == ListenerId;
        });
        if constexpr (Game::Config::bEnableLog) {
            if (RemovedCount > 0) {
                UE_LOG(LogGameplayMessage, Verbose, TEXT("【消息子系统】注销监听器成功: 通道 [%s], 监听器 ID=%lld"), *Channel.ToString(), ListenerId);
            }
        }
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
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameplayMessage, Warning, TEXT("【消息子系统】注册监听器失败：通道标签无效或回调为空。"));
        }
        return FGameplayMessageListenerHandle();
    }

    const int64 ListenerId = ++NextListenerId;
    FChannelListenerList& List = ListenerMap.FindOrAdd(Channel);
    List.Listeners.Add(FListenerEntry{
        ListenerId,
        MatchRule,
        StructType,
        MakeShared<const FRawMessageCallback>(MoveTemp(Callback))
    });

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameplayMessage, Verbose, TEXT("【消息子系统】注册监听器成功: 通道 [%s], 监听器 ID=%lld, 结构体=[%s], 匹配规则=%d"),
            *Channel.ToString(), ListenerId, StructType ? *StructType->GetName() : TEXT("None"), static_cast<int32>(MatchRule));
    }

    return FGameplayMessageListenerHandle(this, Channel, ListenerId);
}

void UGameplayMessageSubsystem::BroadcastMessageInternal(
    FGameplayTag Channel,
    const UScriptStruct* StructType,
    const void* Payload
) {
    check(IsInGameThread());
    if (!Channel.IsValid() || !StructType) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameplayMessage, Warning, TEXT("【消息子系统】广播消息失败：通道标签无效或结构体类型为空。"));
        }
        return;
    }

    TArray<TSharedPtr<const FRawMessageCallback>, TInlineAllocator<16>> CallbacksToInvoke;

    auto CheckAndAdd = [&CallbacksToInvoke, StructType](const FListenerEntry& Entry) {
        if (!Entry.Callback.IsValid()) {
            return;
        }
        if (const UScriptStruct* ExpectedStruct = Entry.ExpectedStructType.Get()) {
            if (StructType->IsChildOf(ExpectedStruct)) {
                CallbacksToInvoke.Add(Entry.Callback);
            }
        } else {
            CallbacksToInvoke.Add(Entry.Callback);
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
        const FChannelListenerList* ParentList = ListenerMap.Find(ParentTag);
        if (ParentList == nullptr) {
            continue;
        }
        for (const FListenerEntry& Entry : ParentList->Listeners) {
            if (Entry.MatchRule == EGameplayMessageMatch::PartialMatch) {
                CheckAndAdd(Entry);
            }
        }
    }

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameplayMessage, Verbose, TEXT("【消息子系统】广播消息: 通道 [%s], 载荷类型=[%s], 匹配触发监听器数=%d"),
            *Channel.ToString(), *StructType->GetName(), CallbacksToInvoke.Num());
    }

    for (const TSharedPtr<const FRawMessageCallback>& CallbackPtr : CallbacksToInvoke) {
        (*CallbackPtr)(Channel, StructType, Payload);
    }
}

void UGameplayMessageSubsystem::EnqueuePendingBroadcast(TUniqueFunction<void()> BroadcastTask) {
    if (!BroadcastTask || bIsShuttingDown.load(std::memory_order_acquire)) {
        return;
    }

    PendingMessageQueue.Enqueue(MoveTemp(BroadcastTask));

    bool bExpected = false;
    if (bDrainScheduled.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel)) {
        TWeakObjectPtr<UGameplayMessageSubsystem> WeakSelf(this);
        AsyncTask(ENamedThreads::GameThread, [WeakSelf]() {
            if (UGameplayMessageSubsystem* Self = WeakSelf.Get()) {
                Self->FlushPendingMessages();
            }
        });
    }
}
