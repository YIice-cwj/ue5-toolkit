/**
 * @file FObserverState.h
 * @brief 观察者线程安全内部共享状态模板类（单一职责 · 负责读写锁保护、快照维护与持锁出锁解耦）
 */

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeRWLock.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Async/FGameThreadTaskRunner.h"
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
     * @brief 监听条目结构（包含订阅 ID、回调及弱引用宿主；若绑定 UObject 宿主，订阅与广播应在 GameThread 调用以保证 UObject 生命周期校验线程安全）
     */
    struct FListenerEntry {
    public:
        /**
         * @brief 检查显式绑定的宿主 UObject 是否已失效销毁
         * @return 宿主已销毁返回 true，未绑定宿主或宿主仍有效返回 false
         */
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

    /**
     * @brief 线程安全注册监听回调并生成新 COW 监听列表快照
     * @param InListener 监听回调闭包
     * @param InOwner 可选绑定宿主弱引用
     * @return 分配的订阅 ID（空回调返回 0）
     */
    uint64 AddListener(FListener InListener, const TWeakObjectPtr<UObject>& InOwner);

    /**
     * @brief 在同一把写锁内完成监听注册并将首帧当前值回放压入通知队列
     * @param InListener 监听回调闭包
     * @param InOwner 可选绑定宿主弱引用
     * @return 包含订阅 ID 与是否需要触发队列排空的结果快照
     */
    [[nodiscard]] FSubscribeSnapshot AddListenerWithReplay(FListener InListener, const TWeakObjectPtr<UObject>& InOwner);

    /**
     * @brief 写入新值、清理失效宿主条目并按需将快照广播任务压入通知队列
     * @param InValue 待写入的新值
     * @param bBroadcast 是否向监听者广播本次变更
     * @return 若由当前调用触发顶层通知队列排空则返回 true
     */
    [[nodiscard]] bool SetValueAndPurge(ConstReference InValue, bool bBroadcast);

    /**
     * @brief 在读锁保护下读取当前值拷贝
     * @return 当前值副本
     */
    [[nodiscard]] ValueType GetValue() const;

    /**
     * @brief 按订阅 ID 移除对应监听者
     * @param SubscriptionId 待移除的订阅 ID
     */
    void RemoveListener(uint64 SubscriptionId) override;

    /**
     * @brief 扫描并移除所有宿主已销毁的失效监听条目
     */
    void PurgeInvalidListeners();

    /**
     * @brief 清理失效监听条目后返回当前存活监听者数量
     * @return 存活监听者数量
     */
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
        Updated.Reserve(Listeners->Num() + 1);
        Updated.Append(*Listeners);
    } else {
        Updated.Reserve(1);
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
        Updated.Reserve(Listeners->Num() + 1);
        Updated.Append(*Listeners);
    } else {
        Updated.Reserve(1);
    }
    Result.SubscriptionId = ++NextId;
    Updated.Add(FListenerEntry{Result.SubscriptionId, MoveTemp(InListener), InOwner});
    Listeners = MakeShared<const TArray<FListenerEntry>>(MoveTemp(Updated));
    Result.bShouldDrain = FNotificationDispatcher::Get().Enqueue(
        [ReplayListener = MoveTemp(ReplayListener), WeakOwner = InOwner, CurrentValue = Value]() {
            if (!WeakOwner.IsExplicitlyNull()) {
                if (IsInGameThread()) {
                    if (WeakOwner.IsValid()) {
                        ReplayListener(CurrentValue);
                    }
                } else {
                    FGameThreadTaskRunner::RunOnGameThread(
                        WeakOwner,
                        [ReplayListener, CurrentValue]() {
                            ReplayListener(CurrentValue);
                        }
                    );
                }
            } else {
                ReplayListener(CurrentValue);
            }
        });
    return Result;
}

template<typename T>
bool FObserverState<T>::SetValueAndPurge(ConstReference InValue, bool bBroadcast) {
    FWriteScopeLock WriteLock(ListenersLock);
    if (Listeners.IsValid() && IsInGameThread()) {
        bool bHasExpired = false;
        for (const FListenerEntry& Entry : *Listeners) {
            if (Entry.IsOwnerExpired()) {
                bHasExpired = true;
                break;
            }
        }
        if (bHasExpired) {
            TArray<FListenerEntry> Updated = *Listeners;
            Updated.RemoveAll([](const FListenerEntry& Entry) { return Entry.IsOwnerExpired(); });
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
                if (!Entry.Owner.IsExplicitlyNull()) {
                    if (IsInGameThread()) {
                        if (Entry.Owner.IsValid()) {
                            Entry.Callback(ValueSnapshot);
                        }
                    } else {
                        FGameThreadTaskRunner::RunOnGameThread(
                            Entry.Owner,
                            [Callback = Entry.Callback, ValueSnapshot]() {
                                Callback(ValueSnapshot);
                            }
                        );
                    }
                } else {
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
    if (!IsInGameThread()) {
        return;
    }
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
    if (IsInGameThread()) {
        PurgeInvalidListeners();
    }
    FReadScopeLock ReadLock(ListenersLock);
    return Listeners.IsValid() ? Listeners->Num() : 0;
}

