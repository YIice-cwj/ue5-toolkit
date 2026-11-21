/**
 * @file FObserverSubscription.h
 * @brief RAII 订阅守卫（FValueObserver / FListenerHub / FConfigRegistry 共用）
 *
 * 设计思路：订阅守卫持 TWeakPtr<FObserverStateBase> 弱引用——观察者先销毁时守卫析构安全跳过；
 * 退订按唯一 uint64 订阅 ID 匹配（TFunction 无可靠 operator==，按值匹配不可用）；
 * 观察者本身为值语义对象（可作全局/子系统成员/栈对象），内部状态由共享指针持有。
 */

#pragma once

#include "CoreMinimal.h"
#include "Listener/FObserverStateBase.h"

template<typename T>
class FObserverState;

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
     * @brief 获取当前订阅的唯一 ID
     * @return 64 位订阅 ID（0 表示未初始化或已注销）
     */
    [[nodiscard]] uint64 GetSubscriptionId() const noexcept {
        return SubscriptionId;
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

    /**
     * @brief 绑定观察者内部状态与订阅 ID（供友元观察者设施初始化守卫）
     * @param InState 观察者状态基类共享引用
     * @param InSubscriptionId 分配的唯一订阅 ID
     */
    void Initialize(const TSharedRef<FObserverStateBase>& InState, uint64 InSubscriptionId);

private:
    TWeakPtr<FObserverStateBase> StateWeak;
    uint64 SubscriptionId = 0;
};
