/**
 * @file FObserverState.h
 * @brief 观察者线程安全内部共享状态模板类（单一职责 · 负责读写锁保护、快照维护与持锁出锁解耦）
 */

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeRWLock.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Listener/FNotificationDispatcher.h"
#include "Listener/FObserverSubscription.h"

class UObject;

template<typename T>
class FValueObserver;

/**
 * @brief 模板状态：值 + 回调条目数组
 * @tparam T 值类型
 */
template<typename T>
class FObserverState : public FObserverStateBase {
public:
    using ValueType = T;
    using ConstReference = const T&;
    using FListener = TFunction<void(ConstReference)>;

    /**
     * @brief 监听条目结构（包含订阅 ID、回调及弱引用宿主）
     */
    struct FListenerEntry {
    public:
        [[nodiscard]] bool IsOwnerExpired() const {
            return !Owner.IsExplicitlyNull() && !Owner.IsValid();
        }

    public:
        uint64 SubscriptionId = 0;
        FListener Callback;
        TWeakObjectPtr<UObject> Owner;
    };

    /**
     * @brief 原子注册并将当前值回放追加到全局 FIFO 的结果结构
     */
    struct FSubscribeSnapshot {
        uint64 SubscriptionId = 0;
        bool bShouldDrain = false;
    };

public:
    FObserverState() = default;

private:
    template<typename> friend class FValueObserver;

    uint64 AddListener(FListener InListener, const TWeakObjectPtr<UObject>& InOwner);
    [[nodiscard]] FSubscribeSnapshot AddListenerWithReplay(FListener InListener, const TWeakObjectPtr<UObject>& InOwner);
    [[nodiscard]] bool SetValueAndPurge(ConstReference InValue, bool bBroadcast);
    [[nodiscard]] ValueType GetValue() const;
    void RemoveListener(uint64 SubscriptionId) override;
    void PurgeInvalidListeners();
    [[nodiscard]] int32 NumListeners();

private:
    ValueType Value{};
    TSharedPtr<const TArray<FListenerEntry>> Listeners;
    uint64 NextId = 0;
    mutable FRWLock ListenersLock;
};

template<typename T>
uint64 FObserverState<T>::AddListener(FListener InListener, const TWeakObjectPtr<UObject>& InOwner) {
    if (!InListener) {
        return 0;
    }
    FWriteScopeLock WriteLock(ListenersLock);
    TArray<FListenerEntry> Updated;
    if (Listeners.IsValid()) {
        Updated = *Listeners;
    }
    const uint64 SubscriptionId = ++NextId;
    Updated.Add(FListenerEntry{SubscriptionId, MoveTemp(InListener), InOwner});
    Listeners = MakeShared<const TArray<FListenerEntry>>(MoveTemp(Updated));
    return SubscriptionId;
}

template<typename T>
typename FObserverState<T>::FSubscribeSnapshot FObserverState<T>::AddListenerWithReplay(
    FListener InListener, const TWeakObjectPtr<UObject>& InOwner) {
    FWriteScopeLock WriteLock(ListenersLock);
    FSubscribeSnapshot Result;
    if (!InListener) {
        return Result;
    }
    FListener ReplayListener = InListener;
    TArray<FListenerEntry> Updated;
    if (Listeners.IsValid()) {
        Updated = *Listeners;
    }
    Result.SubscriptionId = ++NextId;
    Updated.Add(FListenerEntry{Result.SubscriptionId, MoveTemp(InListener), InOwner});
    Listeners = MakeShared<const TArray<FListenerEntry>>(MoveTemp(Updated));
    Result.bShouldDrain = FNotificationDispatcher::Get().Enqueue(
        [ReplayListener = MoveTemp(ReplayListener), WeakOwner = InOwner, CurrentValue = Value]() {
            if (WeakOwner.IsExplicitlyNull() || WeakOwner.IsValid()) {
                ReplayListener(CurrentValue);
            }
        });
    return Result;
}

template<typename T>
bool FObserverState<T>::SetValueAndPurge(ConstReference InValue, bool bBroadcast) {
    FWriteScopeLock WriteLock(ListenersLock);
    if (Listeners.IsValid()) {
        TArray<FListenerEntry> Updated = *Listeners;
        if (Updated.RemoveAll([](const FListenerEntry& Entry) { return Entry.IsOwnerExpired(); }) > 0) {
            Listeners = MakeShared<const TArray<FListenerEntry>>(MoveTemp(Updated));
        }
    }
    Value = InValue;
    if (!bBroadcast || !Listeners.IsValid() || Listeners->IsEmpty()) {
        return false;
    }
    return FNotificationDispatcher::Get().Enqueue(
        [Snapshot = Listeners, ValueSnapshot = Value]() {
            for (const FListenerEntry& Entry : *Snapshot) {
                if (!Entry.IsOwnerExpired()) {
                    Entry.Callback(ValueSnapshot);
                }
            }
        });
}

template<typename T>
typename FObserverState<T>::ValueType FObserverState<T>::GetValue() const {
    FReadScopeLock ReadLock(ListenersLock);
    return Value;
}

template<typename T>
void FObserverState<T>::RemoveListener(uint64 SubscriptionId) {
    FWriteScopeLock WriteLock(ListenersLock);
    if (!Listeners.IsValid()) {
        return;
    }
    TArray<FListenerEntry> Updated = *Listeners;
    const int32 RemovedCount = Updated.RemoveAll([SubscriptionId](const FListenerEntry& Entry) {
        return Entry.SubscriptionId == SubscriptionId;
    });
    if (RemovedCount > 0) {
        Listeners = MakeShared<const TArray<FListenerEntry>>(MoveTemp(Updated));
    }
}

template<typename T>
void FObserverState<T>::PurgeInvalidListeners() {
    FWriteScopeLock WriteLock(ListenersLock);
    if (!Listeners.IsValid()) {
        return;
    }
    TArray<FListenerEntry> Updated = *Listeners;
    if (Updated.RemoveAll([](const FListenerEntry& Entry) { return Entry.IsOwnerExpired(); }) > 0) {
        Listeners = MakeShared<const TArray<FListenerEntry>>(MoveTemp(Updated));
    }
}

template<typename T>
int32 FObserverState<T>::NumListeners() {
    PurgeInvalidListeners();
    FReadScopeLock ReadLock(ListenersLock);
    return Listeners.IsValid() ? Listeners->Num() : 0;
}

