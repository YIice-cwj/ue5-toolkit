/**
 * @file FObserverStateBase.h
 * @brief 观察者状态非模板基类（供 FObserverSubscription / FValueObserver / FListenerHub / FConfigRegistry 共用）
 */

#pragma once

#include "CoreMinimal.h"

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
