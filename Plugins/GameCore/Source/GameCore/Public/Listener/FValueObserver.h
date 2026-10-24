/**
 * @file FValueObserver.h
 * @brief 通用值观察者（继承统一监听基类）：值 + 类型化多播 + RAII 订阅守卫
 *
 * 设计思路：模板类严禁加导出宏（dllexport 不可用于未特化类模板），故 FValueObserver /
 * FObserverState 实现全部定义于本头文件类外；仅非模板基类 FObserverStateBase、FObserverBase 与守卫
 * FObserverSubscription 导出。观察者继承 FObserverBase 获取名称/启停开关键/版本号；
 * 内部状态由 TSharedRef<FObserverState<T>> 共享持有，订阅守卫持
 * TWeakPtr<FObserverStateBase> 弱引用——观察者先销毁时守卫析构安全跳过；
 * 订阅条目可携带 Owner 弱引用：Owner 销毁后广播自动跳过，条目在下一次 Set 或计数查询时惰性清理。
 */

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeRWLock.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "Listener/FObserverBase.h"
#include "Listener/FObserverSubscription.h"

class UObject;

template<typename T>
class FValueObserver;

/**
 * @brief 模板状态：值 + 回调条目数组（继承自非模板基类以支持导出与弱引用）
 * @tparam T 值类型
 *
 * 实现思路：值与监听容器快照统一由 FRWLock 保护——广播在读锁内拷贝条目快照与当前值副本后立即出锁派发，
 * 回调内增删监听或重入读取不影响本次遍历，且持锁期间绝不执行用户回调。
 */
template<typename T>
class FObserverState : public FObserverStateBase {
public:
    using ValueType = T;
    using ConstReference = const T&;
    using FListener = TFunction<void(ConstReference)>;

private:
    /** 监听条目：唯一 ID + 回调 + Owner 弱引用（退订只能按 ID 匹配；Owner 为空表示不绑定生命周期） */
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

    /** 原子注册并抓取当前值快照的结果结构 */
    struct FSubscribeSnapshot {
        uint64 SubscriptionId = 0;
        ValueType CurrentValue{};
    };

public:
    FObserverState() = default;

private:
    template<typename> friend class FValueObserver;

    uint64 AddListener(FListener InListener, const TWeakObjectPtr<UObject>& InOwner);
    [[nodiscard]] FSubscribeSnapshot AddListenerWithValueSnapshot(FListener InListener, const TWeakObjectPtr<UObject>& InOwner);
    void SetValueAndPurge(ConstReference InValue);
    [[nodiscard]] ValueType GetValue() const;
    void Broadcast() const;
    void RemoveListener(uint64 SubscriptionId) override;

    /** 清理 Owner 已销毁的失效条目（写锁内整体替换快照） */
    void PurgeInvalidListeners();

    /** 清理失效条目后返回有效条目数量 */
    [[nodiscard]] int32 NumListeners();

private:
    ValueType Value{};
    TSharedPtr<const TArray<FListenerEntry>> Listeners;
    uint64 NextId = 0;
    mutable FRWLock ListenersLock;
};

/**
 * @brief 通用值观察者模板（全内联，无导出宏）
 * @tparam T 值类型
 */
template<typename T>
class FValueObserver : public FObserverBase {
public:
    using ValueType = T;
    using ConstReference = const T&;
    using FListener = TFunction<void(ConstReference)>;

    /**
     * @brief 构造（继承基类注册开关键，创建共享状态）
     * @param ObserverName 监听器名称（开关键 Listener.{Name}.Enabled）
     */
    explicit FValueObserver(const FName& ObserverName);

    /**
     * @brief 赋值并广播（经基类启停校验与版本自增）
     * @param InValue 新值
     */
    void Set(ConstReference InValue);

    /**
     * @brief 线程安全获取当前值副本
     * @return 当前值
     */
    [[nodiscard]] ValueType Get() const;

    /**
     * @brief 注册监听
     * @param InListener 回调（收到新值）
     * @return RAII 订阅守卫，业务方必须持有；析构守卫即注销
     */
    TSharedRef<FObserverSubscription> Subscribe(FListener InListener);

    /**
     * @brief 注册监听并绑定 Owner 生命周期（Owner 销毁后不再回调；非空 Owner 须在游戏线程调用）
     * @param Owner 订阅归属对象（为空表示不绑定生命周期）
     * @param InListener 回调（收到新值）
     * @return RAII 订阅守卫，业务方必须持有；析构守卫即注销
     */
    TSharedRef<FObserverSubscription> Subscribe(UObject* Owner, FListener InListener);

    /**
     * @brief 注册监听并立即回放当前值（订阅即得当前值，消除“订阅前已就绪”的漏读窗口）
     * @param InListener 回调（先收到当前值，之后收到每次更新）
     * @return RAII 订阅守卫，业务方必须持有；析构守卫即注销
     */
    TSharedRef<FObserverSubscription> SubscribeWithReplay(FListener InListener);

    /**
     * @brief 注册监听并立即回放当前值（Owner 销毁后不再回调；非空 Owner 须在游戏线程调用）
     * @param Owner 订阅归属对象（为空表示不绑定生命周期）
     * @param InListener 回调（先收到当前值，之后收到每次更新）
     * @return RAII 订阅守卫，业务方必须持有；析构守卫即注销
     */
    TSharedRef<FObserverSubscription> SubscribeWithReplay(UObject* Owner, FListener InListener);

    /**
     * @brief 查询监听条目数量（先清理 Owner 已销毁的失效条目）
     * @return 有效监听条目数量
     */
    [[nodiscard]] int32 NumListeners();

private:
    template<typename> friend class FObserverState;

    /** 类型化广播分发（继承基类启停校验与版本自增） */
    void Notify() override;

private:
    /** 共享状态（值 + 类型化多播） */
    TSharedRef<FObserverState<ValueType>> State;
};

// ---------------------------------------------------------------------------
// 模板实现（定义全部置于类外，顺序与声明严格 1:1 对齐）
// ---------------------------------------------------------------------------

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
typename FObserverState<T>::FSubscribeSnapshot FObserverState<T>::AddListenerWithValueSnapshot(
    FListener InListener, const TWeakObjectPtr<UObject>& InOwner) {
    FWriteScopeLock WriteLock(ListenersLock);
    FSubscribeSnapshot Result;
    Result.CurrentValue = Value;
    if (!InListener) {
        return Result;
    }
    TArray<FListenerEntry> Updated;
    if (Listeners.IsValid()) {
        Updated = *Listeners;
    }
    Result.SubscriptionId = ++NextId;
    Updated.Add(FListenerEntry{Result.SubscriptionId, MoveTemp(InListener), InOwner});
    Listeners = MakeShared<const TArray<FListenerEntry>>(MoveTemp(Updated));
    return Result;
}

template<typename T>
void FObserverState<T>::SetValueAndPurge(ConstReference InValue) {
    FWriteScopeLock WriteLock(ListenersLock);
    if (Listeners.IsValid()) {
        TArray<FListenerEntry> Updated = *Listeners;
        if (Updated.RemoveAll([](const FListenerEntry& Entry) { return Entry.IsOwnerExpired(); }) > 0) {
            Listeners = MakeShared<const TArray<FListenerEntry>>(MoveTemp(Updated));
        }
    }
    Value = InValue;
}

template<typename T>
typename FObserverState<T>::ValueType FObserverState<T>::GetValue() const {
    FReadScopeLock ReadLock(ListenersLock);
    return Value;
}

template<typename T>
void FObserverState<T>::Broadcast() const {
    TSharedPtr<const TArray<FListenerEntry>> Snapshot;
    ValueType ValueSnapshot{};
    {
        FReadScopeLock ReadLock(ListenersLock);
        Snapshot = Listeners;
        ValueSnapshot = Value;
    }
    if (!Snapshot.IsValid()) {
        return;
    }
    for (const FListenerEntry& Entry : *Snapshot) {
        if (!Entry.IsOwnerExpired()) {
            Entry.Callback(ValueSnapshot);
        }
    }
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

template<typename T>
FValueObserver<T>::FValueObserver(const FName& ObserverName)
    : FObserverBase(ObserverName),
      State(MakeShared<FObserverState<ValueType>>()) {}

template<typename T>
void FValueObserver<T>::Set(ConstReference InValue) {
    State->SetValueAndPurge(InValue);
    Broadcast();
}

template<typename T>
typename FValueObserver<T>::ValueType FValueObserver<T>::Get() const {
    return State->GetValue();
}

template<typename T>
TSharedRef<FObserverSubscription> FValueObserver<T>::Subscribe(FListener InListener) {
    return Subscribe(nullptr, MoveTemp(InListener));
}

template<typename T>
TSharedRef<FObserverSubscription> FValueObserver<T>::Subscribe(UObject* Owner, FListener InListener) {
    const TSharedRef<FObserverSubscription> Subscription = MakeShared<FObserverSubscription>();
    Subscription->Initialize(State, State->AddListener(MoveTemp(InListener), TWeakObjectPtr<UObject>(Owner)));
    return Subscription;
}

template<typename T>
TSharedRef<FObserverSubscription> FValueObserver<T>::SubscribeWithReplay(FListener InListener) {
    return SubscribeWithReplay(nullptr, MoveTemp(InListener));
}

/**
 * @brief 注册监听并立即回放当前值（Owner 感知）
 * @param Owner 订阅归属对象（为空表示不绑定生命周期）
 * @param InListener 回调
 * @return RAII 订阅守卫
 *
 * 实现思路：在写锁内原子完成监听注入与当前值副本抓取，出锁后回放；
 * Owner 在订阅时已失效则跳过回放，之后每次 Set 的广播由条目中的弱引用校验过滤。
 */
template<typename T>
TSharedRef<FObserverSubscription> FValueObserver<T>::SubscribeWithReplay(UObject* Owner, FListener InListener) {
    FListener ReplayListener = InListener;
    const TWeakObjectPtr<UObject> WeakOwner(Owner);
    const typename FObserverState<ValueType>::FSubscribeSnapshot Snapshot =
        State->AddListenerWithValueSnapshot(MoveTemp(InListener), WeakOwner);
    const TSharedRef<FObserverSubscription> Subscription = MakeShared<FObserverSubscription>();
    Subscription->Initialize(State, Snapshot.SubscriptionId);
    const bool bOwnerAlive = Owner == nullptr || WeakOwner.IsValid();
    if (ReplayListener && bOwnerAlive) {
        ReplayListener(Snapshot.CurrentValue);
    }
    return Subscription;
}

template<typename T>
int32 FValueObserver<T>::NumListeners() {
    return State->NumListeners();
}

template<typename T>
void FValueObserver<T>::Notify() {
    State->Broadcast();
}
