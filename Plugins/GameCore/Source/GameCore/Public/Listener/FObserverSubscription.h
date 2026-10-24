/**
 * @file FObserverSubscription.h
 * @brief 观察者状态非模板基类 + RAII 订阅守卫（FValueObserver / FListenerHub / FConfigRegistry 共用）
 *
 * 设计思路：订阅守卫持 TWeakPtr<FObserverStateBase> 弱引用——观察者先销毁时守卫析构安全跳过；
 * 退订按唯一 uint64 订阅 ID 匹配（TFunction 无可靠 operator==，按值匹配不可用）；
 * 观察者本身为值语义对象（可作全局/子系统成员/栈对象），内部状态由共享指针持有。
 */

#pragma once

#include "CoreMinimal.h"

template<typename T>
class FObserverState;

/**
 * @brief 观察者状态非模板基类：仅承担导出与订阅移除契约
 */
class GAMECORE_API FObserverStateBase {
public:
    FObserverStateBase() = default;
    virtual ~FObserverStateBase() = default;
    FObserverStateBase(const FObserverStateBase&) = delete;
    FObserverStateBase& operator=(const FObserverStateBase&) = delete;
    FObserverStateBase(FObserverStateBase&&) = delete;
    FObserverStateBase& operator=(FObserverStateBase&&) = delete;

    /**
     * @brief 按订阅 ID 移除一条监听（子类实现，具体移除其回调条目）
     * @param SubscriptionId 订阅 ID
     */
    virtual void RemoveListener(uint64 SubscriptionId) = 0;
};

/**
 * @brief RAII 订阅守卫：析构自动注销；禁止拷贝，允许移动
 *
 * 使用方式：Subscribe 返回后必须由业务方持有，销毁守卫即注销监听。
 */
class [[nodiscard]] GAMECORE_API FObserverSubscription {
public:
    FObserverSubscription() = default;
    ~FObserverSubscription();

    FObserverSubscription(const FObserverSubscription&) = delete;
    FObserverSubscription& operator=(const FObserverSubscription&) = delete;
    FObserverSubscription(FObserverSubscription&& Other) noexcept;
    FObserverSubscription& operator=(FObserverSubscription&& Other) noexcept;

    /**
     * @brief 注销监听并清空订阅句柄
     */
    void Reset();

    /**
     * @brief 查询订阅是否处于有效活跃状态
     * @return 状态指针有效且订阅 ID 非零返回 true
     */
    [[nodiscard]] bool IsActive() const noexcept {
        return SubscriptionId != 0 && StateWeak.IsValid();
    }

    /**
     * @brief 布尔显式类型转换操作符
     * @return 订阅有效返回 true
     */
    explicit operator bool() const noexcept {
        return IsActive();
    }

private:
    template<typename> friend class FValueObserver;
    friend class FListenerHub;
    friend class FConfigRegistry;

    void Initialize(const TSharedRef<FObserverStateBase>& InState, uint64 InSubscriptionId);

private:
    TWeakPtr<FObserverStateBase> StateWeak;
    uint64 SubscriptionId = 0;
};
