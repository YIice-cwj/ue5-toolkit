// Copyright Epic Games, Inc. All Rights Reserved.

#include "Message/GameplayMessageSubsystem.h"

#include "Async/Async.h"
#include "Config/GameCompileSwitches.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameplayMessage, Log, All);

/**
 * @brief 获取子系统单例引用（断言有效）
 * @param WorldContextObject 关联的世界上下文对象
 * @return 子系统引用
 */
UGameplayMessageSubsystem& UGameplayMessageSubsystem::Get(const UObject* WorldContextObject) {
    UGameplayMessageSubsystem* Subsystem = GetPtr(WorldContextObject);
    check(Subsystem != nullptr);
    return *Subsystem;
}

/**
 * @brief 安全获取子系统指针（可空）
 * @param WorldContextObject 关联的世界上下文对象
 * @return 子系统指针，未就绪时返回 nullptr
 */
UGameplayMessageSubsystem* UGameplayMessageSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (!WorldContextObject) {
        return nullptr;
    }
    if (const UGameInstance* GameInstance = Cast<UGameInstance>(WorldContextObject)) {
        return GameInstance->GetSubsystem<UGameplayMessageSubsystem>();
    }
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)) {
        if (UGameInstance* GameInstance = World->GetGameInstance()) {
            return GameInstance->GetSubsystem<UGameplayMessageSubsystem>();
        }
    }
    return nullptr;
}

/**
 * @brief 子系统初始化生命周期回调：重置跨线程队列运行标志
 * @param Collection 子系统集合
 */
void UGameplayMessageSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    bIsShuttingDown.store(false, std::memory_order_release);
}

/**
 * @brief 子系统销毁生命周期回调：关闭跨线程入队通道、排空丢弃残留任务并清空所有监听器
 */
void UGameplayMessageSubsystem::Deinitialize() {
    bIsShuttingDown.store(true, std::memory_order_release);
    while (PendingMessageQueue.Dequeue().IsSet()) {
    }
    bDrainScheduled.store(false, std::memory_order_release);
    PartialMatchListenerCount = 0;
    NextListenerId = 0;

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameplayMessage, Log, TEXT("【消息子系统】子系统注销，清空 %d 个通道的监听器。"), ListenerMap.Num());
    }
    ListenerMap.Empty();
    Super::Deinitialize();
}

/**
 * @brief 检查指定通道是否存在任何监听器
 * @param Channel 目标通道
 * @param MatchRule 匹配模式（默认精确匹配）
 * @return 存在匹配监听器返回 true
 */
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

    if (MatchRule != EGameplayMessageMatch::PartialMatch || PartialMatchListenerCount == 0) {
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

/**
 * @brief 在主线程立即排空并派发所有跨线程待处理消息队列
 * @return 本次排空实际派发的消息数量
 */
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

/**
 * @brief 手动注销指定通道上的监听器 ID
 * @param Channel 目标通道
 * @param ListenerId 监听器唯一 ID
 * @return 成功注销返回 Success，未找到对应监听器返回 Failure
 */
TGameStatusResult<void> UGameplayMessageSubsystem::UnregisterListener(FGameplayTag Channel, int64 ListenerId) {
    check(IsInGameThread());
    if (ListenerId == 0) {
        return TGameStatusResult<void>::Failure(1, TEXT("监听器句柄 ID 为 0"));
    }
    if (FChannelListenerList* List = ListenerMap.Find(Channel)) {
        const int32 RemovedCount = List->Listeners.RemoveAll([this, ListenerId](const FListenerEntry& Entry) {
            if (Entry.Id == ListenerId) {
                if (Entry.MatchRule == EGameplayMessageMatch::PartialMatch) {
                    --PartialMatchListenerCount;
                }
                return true;
            }
            return false;
        });
        if constexpr (Game::Config::bEnableLog) {
            if (RemovedCount > 0) {
                UE_LOG(LogGameplayMessage, Verbose, TEXT("【消息子系统】注销监听器成功: 通道 [%s], 监听器 ID=%lld"), *Channel.ToString(), ListenerId);
            }
        }
        if (List->Listeners.IsEmpty()) {
            ListenerMap.Remove(Channel);
        }
        if (RemovedCount > 0) {
            return TGameStatusResult<void>::Success();
        }
    }
    return TGameStatusResult<void>::Failure(2, FString::Printf(TEXT("通道 %s 上未找到监听器 %lld"), *Channel.ToString(), ListenerId));
}

/**
 * @brief 查询当前已注册的监听器总数（供测试或诊断使用）
 * @return 监听器数量
 */
int32 UGameplayMessageSubsystem::GetListenerCount() const {
    check(IsInGameThread());
    int32 Count = 0;
    for (const auto& Pair : ListenerMap) {
        Count += Pair.Value.Listeners.Num();
    }
    return Count;
}

/**
 * @brief 内部注册监听器实现函数
 * @param Channel 注册目标通道
 * @param StructType 期望匹配的反射结构体元数据指针（可空代表通配）
 * @param Callback 底层擦除类型的函数闭包
 * @param MatchRule 匹配模式
 * @return RAII 订阅句柄
 */
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
    if (MatchRule == EGameplayMessageMatch::PartialMatch) {
        ++PartialMatchListenerCount;
    }
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

/**
 * @brief 内部消息广播核心派发函数
 * @param Channel 广播通道
 * @param StructType 载荷反射结构体类型
 * @param Payload 载荷内存裸指针
 * @return 广播派发成功返回 Success，参数校验失败返回 Failure
 */
TGameStatusResult<void> UGameplayMessageSubsystem::BroadcastMessageInternal(
    FGameplayTag Channel,
    const UScriptStruct* StructType,
    const void* Payload
) {
    check(IsInGameThread());
    if (!Channel.IsValid()) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameplayMessage, Warning, TEXT("【消息子系统】广播消息失败：通道标签无效。"));
        }
        return TGameStatusResult<void>::Failure(1, TEXT("消息通道标签无效"));
    }
    if (!StructType) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameplayMessage, Warning, TEXT("【消息子系统】广播消息失败：结构体类型为空。"));
        }
        return TGameStatusResult<void>::Failure(2, TEXT("消息结构体类型为空"));
    }
    if (!Payload) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameplayMessage, Warning, TEXT("【消息子系统】广播消息失败：载荷指针为空。"));
        }
        return TGameStatusResult<void>::Failure(3, TEXT("载荷数据指针为空"));
    }

    TArray<TSharedPtr<const FRawMessageCallback>, TInlineAllocator<16>> CallbacksToInvoke;

    auto CheckAndAdd = [&CallbacksToInvoke, StructType](const FListenerEntry& Entry) {
        if (!Entry.Callback.IsValid() || Entry.ExpectedStructType.IsStale()) {
            return;
        }
        if (const UScriptStruct* ExpectedStruct = Entry.ExpectedStructType.Get()) {
            if (StructType == ExpectedStruct || StructType->IsChildOf(ExpectedStruct)) {
                CallbacksToInvoke.Add(Entry.Callback);
            }
        } else if (Entry.ExpectedStructType.IsExplicitlyNull()) {
            CallbacksToInvoke.Add(Entry.Callback);
        }
    };

    if (const FChannelListenerList* ExactList = ListenerMap.Find(Channel)) {
        for (const FListenerEntry& Entry : ExactList->Listeners) {
            CheckAndAdd(Entry);
        }
    }

    if (PartialMatchListenerCount > 0) {
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
    }

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameplayMessage, Verbose, TEXT("【消息子系统】广播消息: 通道 [%s], 载荷类型=[%s], 匹配触发监听器数=%d"),
            *Channel.ToString(), *StructType->GetName(), CallbacksToInvoke.Num());
    }

    for (const TSharedPtr<const FRawMessageCallback>& CallbackPtr : CallbacksToInvoke) {
        (*CallbackPtr)(Channel, StructType, Payload);
    }
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 内部跨线程待广播任务入队调度函数
 * @param BroadcastTask 待在游戏线程执行的广播任务
 */
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
