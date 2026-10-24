/**
 * @file FObserverSubscription.cpp
 * @brief RAII 订阅守卫非模板实现（模板实现见 FValueObserver.h / FListenerHub.h）
 */

#include "Listener/FObserverSubscription.h"

FObserverSubscription::~FObserverSubscription() {
    Reset();
}

FObserverSubscription::FObserverSubscription(FObserverSubscription&& Other) noexcept
    : StateWeak(MoveTemp(Other.StateWeak)),
      SubscriptionId(Other.SubscriptionId) {
    Other.SubscriptionId = 0;
    Other.StateWeak.Reset();
}

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

void FObserverSubscription::Reset() {
    if (SubscriptionId != 0) {
        if (TSharedPtr<FObserverStateBase> State = StateWeak.Pin()) {
            State->RemoveListener(SubscriptionId);
        }
    }
    SubscriptionId = 0;
    StateWeak.Reset();
}

void FObserverSubscription::Initialize(const TSharedRef<FObserverStateBase>& InState, uint64 InSubscriptionId) {
    StateWeak = InState;
    SubscriptionId = InSubscriptionId;
}
