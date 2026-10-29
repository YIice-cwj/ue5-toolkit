/**
 * @file FGameStatSubscriptionHub.cpp
 * @brief 按 Tag 订阅管理中心实现
 */

#include "Stat/FGameStatSubscriptionHub.h"

FDelegateHandle FGameStatSubscriptionHub::Subscribe(const FGameplayTag& InStatTag, FOnGameStatChangedNative::FDelegate InDelegate) {
    if (!InStatTag.IsValid()) {
        return FDelegateHandle();
    }
    return TaggedStatChanged.FindOrAdd(InStatTag).Add(InDelegate);
}

FDelegateHandle FGameStatSubscriptionHub::Subscribe(
    UObject* InOwner,
    const FGameplayTag& InStatTag,
    TFunction<void(const FGameplayTag&, float, float, float)> InCallback
) {
    if (!InStatTag.IsValid() || InCallback == nullptr) {
        return FDelegateHandle();
    }
    if (InOwner != nullptr && !IsValid(InOwner)) {
        return FDelegateHandle();
    }

    const FDelegateHandle Handle = Subscribe(InStatTag, FOnGameStatChangedNative::FDelegate::CreateLambda(
        [InCallback](const FGameplayTag& Tag, float OldVal, float NewVal, float MaxVal) {
            InCallback(Tag, OldVal, NewVal, MaxVal);
        }
    ));

    if (Handle.IsValid() && InOwner != nullptr) {
        OwnerStatSubscriptions.Add(FStatOwnerSubscription{InStatTag, InOwner, Handle});
    }
    return Handle;
}

void FGameStatSubscriptionHub::Unsubscribe(const FGameplayTag& InStatTag, FDelegateHandle InHandle) {
    if (!InStatTag.IsValid() || !InHandle.IsValid()) {
        return;
    }

    if (FOnGameStatChangedNative* Multicast = TaggedStatChanged.Find(InStatTag)) {
        Multicast->Remove(InHandle);
        if (!Multicast->IsBound()) {
            TaggedStatChanged.Remove(InStatTag);
        }
    }

    OwnerStatSubscriptions.RemoveAll([InHandle](const FStatOwnerSubscription& Entry) {
        return Entry.Handle == InHandle;
    });
}

void FGameStatSubscriptionHub::Broadcast(const FGameplayTag& InStatTag, float OldValue, float NewValue, float MaxValue) {
    PurgeExpiredOwnerSubscriptions(InStatTag);

    if (const FOnGameStatChangedNative* Multicast = TaggedStatChanged.Find(InStatTag)) {
        Multicast->Broadcast(InStatTag, OldValue, NewValue, MaxValue);
    }
}

void FGameStatSubscriptionHub::PurgeExpiredOwnerSubscriptions(const FGameplayTag& InStatTag) {
    for (int32 Index = OwnerStatSubscriptions.Num() - 1; Index >= 0; --Index) {
        const FStatOwnerSubscription& Sub = OwnerStatSubscriptions[Index];
        if (Sub.Tag.MatchesTagExact(InStatTag) && !Sub.Owner.IsValid()) {
            if (FOnGameStatChangedNative* Multicast = TaggedStatChanged.Find(InStatTag)) {
                Multicast->Remove(Sub.Handle);
                if (!Multicast->IsBound()) {
                    TaggedStatChanged.Remove(InStatTag);
                }
            }
            OwnerStatSubscriptions.RemoveAtSwap(Index);
        }
    }
}

void FGameStatSubscriptionHub::Reset() {
    TaggedStatChanged.Reset();
    OwnerStatSubscriptions.Reset();
}
