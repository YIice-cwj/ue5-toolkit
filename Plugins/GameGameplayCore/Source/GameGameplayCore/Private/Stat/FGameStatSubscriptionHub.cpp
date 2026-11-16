/**
 * @file FGameStatSubscriptionHub.cpp
 * @brief 按 Tag 订阅管理中心实现
 */

#include "Stat/FGameStatSubscriptionHub.h"

/**
 * @brief 订阅指定属性的变化（底层委托版）
 * @param InStatTag 目标属性标签
 * @param InDelegate 变化回调委托
 * @return 订阅句柄，参数非法时返回无效句柄
 */
FDelegateHandle FGameStatSubscriptionHub::Subscribe(const FGameplayTag& InStatTag, FOnGameStatChangedNative::FDelegate InDelegate) {
    if (!InStatTag.IsValid()) {
        return FDelegateHandle();
    }
    return TaggedStatChanged.FindOrAdd(InStatTag).Add(InDelegate);
}

/**
 * @brief 订阅指定属性的变化（绑定 Owner 生命周期版）
 * @param InOwner 归属对象（失效时自动停止回调并适时清理）
 * @param InStatTag 目标属性标签
 * @param InCallback 变化回调闭包
 * @return 订阅句柄，参数非法或 Owner 已失效时返回无效句柄
 */
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

/**
 * @brief 注销指定属性订阅
 * @param InStatTag 目标属性标签
 * @param InHandle 订阅句柄
 */
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

/**
 * @brief 向指定属性的订阅者广播变化事件
 * @param InStatTag 目标属性标签
 * @param OldValue 旧值
 * @param NewValue 新值
 * @param MaxValue 上限值
 */
void FGameStatSubscriptionHub::Broadcast(const FGameplayTag& InStatTag, float OldValue, float NewValue, float MaxValue) {
    PurgeExpiredOwnerSubscriptions(InStatTag);

    if (const FOnGameStatChangedNative* Multicast = TaggedStatChanged.Find(InStatTag)) {
        Multicast->Broadcast(InStatTag, OldValue, NewValue, MaxValue);
    }
}

/**
 * @brief 清理指定属性中已失效 Owner 的订阅条目
 * @param InStatTag 目标属性标签
 */
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

/**
 * @brief 清除指定属性标签下的全部订阅者（含原生委托与生命周期绑定条目）
 * @param InStatTag 目标属性标签
 */
void FGameStatSubscriptionHub::ClearTagSubscriptions(const FGameplayTag& InStatTag) {
    if (!InStatTag.IsValid()) {
        return;
    }

    TaggedStatChanged.Remove(InStatTag);

    OwnerStatSubscriptions.RemoveAll([&InStatTag](const FStatOwnerSubscription& Entry) {
        return Entry.Tag.MatchesTagExact(InStatTag);
    });
}

/**
 * @brief 重置全部订阅
 */
void FGameStatSubscriptionHub::Reset() {
    TaggedStatChanged.Reset();
    OwnerStatSubscriptions.Reset();
}
