/**
 * @file FObserverSubscription.cpp
 * @brief RAII 订阅守卫非模板实现（模板实现见 FValueObserver.h / FListenerHub.h）
 */

#include "Listener/FObserverSubscription.h"

/**
 * @brief 析构函数：自动注销订阅并释放关联引用
 */
FObserverSubscription::~FObserverSubscription() {
    Reset();
}

/**
 * @brief 移动构造函数
 * @param Other 待移动的源订阅守卫
 */
FObserverSubscription::FObserverSubscription(FObserverSubscription&& Other) noexcept
    : StateWeak(MoveTemp(Other.StateWeak)),
      SubscriptionId(Other.SubscriptionId) {
    Other.SubscriptionId = 0;
    Other.StateWeak.Reset();
}

/**
 * @brief 移动赋值操作符
 * @param Other 待移动的源订阅守卫
 * @return 自身引用
 */
FObserverSubscription& FObserverSubscription::operator=(FObserverSubscription&& Other) noexcept {
    if (this != &Other) {
        Reset();
        StateWeak = MoveTemp(Other.StateWeak);
        SubscriptionId = Other.SubscriptionId;
        Other.SubscriptionId = 0;
        Other.StateWeak.Reset();
    }
    return *this;
}

/**
 * @brief 注销监听并清空订阅句柄
 */
void FObserverSubscription::Reset() {
    const uint64 IdToReset = SubscriptionId;
    SubscriptionId = 0;
    if (IdToReset != 0) {
        if (TSharedPtr<FObserverStateBase> State = StateWeak.Pin()) {
            State->RemoveListener(IdToReset);
        }
    }
    StateWeak.Reset();
}

/**
 * @brief 初始化订阅守卫（关联目标状态与订阅 ID）
 * @param InState 观察者状态强引用
 * @param InSubscriptionId 唯一订阅 ID
 */
void FObserverSubscription::Initialize(const TSharedRef<FObserverStateBase>& InState, uint64 InSubscriptionId) {
    Reset();
    StateWeak = InState;
    SubscriptionId = InSubscriptionId;
}
