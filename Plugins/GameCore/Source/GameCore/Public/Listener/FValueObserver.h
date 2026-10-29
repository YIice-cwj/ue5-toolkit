/**
 * @file FValueObserver.h
 * @brief 通用值观察者（继承统一监听基类）：值 + 类型化多播 + RAII 订阅守卫
 */

#pragma once

#include "CoreMinimal.h"
#include "Listener/FObserverBase.h"
#include "Listener/FObserverSubscription.h"
#include "Listener/FObserverState.h"

class UObject;

/**
 * @brief 通用值观察者模板（声明与接口契约）
 * @tparam T 被观察的值类型
 */
template<typename T>
class FValueObserver : public FObserverBase {
public:
    using ValueType = T;
    using ConstReference = const T&;
    using FListener = TFunction<void(ConstReference)>;

    /**
     * @brief 构造通用值观察者实例
     * @param ObserverName 观察者唯一名称（用于生成对应的开关配置键 Listener.{Name}.Enabled）
     */
    explicit FValueObserver(const FName& ObserverName);

    /**
     * @brief 线程安全设置新值并在出锁后广播给全部活跃订阅者
     * @param InValue 待赋予的新值常量引用
     */
    void Set(ConstReference InValue);

    /**
     * @brief 线程安全获取当前值的副本（锁内拷贝后出锁，调用方持有独立副本）
     * @return 当前值的深拷贝
     */
    [[nodiscard]] ValueType Get() const;

    /**
     * @brief 注册监听回调
     * @param InListener 接收到新值时的回调闭包
     * @return RAII 订阅守卫共享引用；业务方持有此守卫以维持订阅，析构即自动注销
     */
    TSharedRef<FObserverSubscription> Subscribe(FListener InListener);

    /**
     * @brief 注册监听回调并绑定 Owner 生命周期（Owner 销毁后回调自动跳过）
     * @param Owner 归属的 UObject 对象指针（弱引用追踪，失效后自动跳过回调）
     * @param InListener 接收到新值时的回调闭包
     * @return RAII 订阅守卫共享引用
     */
    TSharedRef<FObserverSubscription> Subscribe(UObject* Owner, FListener InListener);

    /**
     * @brief 注册监听回调并在订阅瞬间立即回放当前值
     * @param InListener 接收到新值时的回调闭包
     * @return RAII 订阅守卫共享引用
     */
    TSharedRef<FObserverSubscription> SubscribeWithReplay(FListener InListener);

    /**
     * @brief 注册监听回调并立即回放当前值（绑定 Owner 生命周期）
     * @param Owner 归属的 UObject 对象指针
     * @param InListener 接收到新值时的回调闭包
     * @return RAII 订阅守卫共享引用
     */
    TSharedRef<FObserverSubscription> SubscribeWithReplay(UObject* Owner, FListener InListener);

    /**
     * @brief 查询当前已注册的活跃监听条目总数
     * @return 监听者数量
     */
    [[nodiscard]] int32 NumListeners();

private:
    template<typename> friend class FObserverState;

    void Notify() override;

private:
    TSharedRef<FObserverState<ValueType>> State;
};

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
