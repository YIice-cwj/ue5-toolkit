/**
 * @file FGameStatSubscriptionHub.cpp
 * @brief 按 Tag 订阅管理中心实现
 */

#include "Stat/FGameStatSubscriptionHub.h"

/**
 * @brief 订阅指定属性的变化（底层委托版）
 * @param InStatTag 目标属性标签
 * @param InDelegate 变化回调委托
 * @return 订阅句柄；标签或委托无效时返回无效句柄
 */
FDelegateHandle FGameStatSubscriptionHub::Subscribe(
    const FGameplayTag& InStatTag,
    FOnGameStatChangedNative::FDelegate InDelegate
) {
    if (!InStatTag.IsValid() || !InDelegate.IsBound()) {
        return FDelegateHandle();
    }

    if (TSharedRef<FOnGameStatChangedNative>* Existing = TaggedStatChanged.Find(InStatTag)) {
        return (*Existing)->Add(MoveTemp(InDelegate));
    }

    const TSharedRef<FOnGameStatChangedNative> Created = MakeShared<FOnGameStatChangedNative>();
    const FDelegateHandle Handle = Created->Add(MoveTemp(InDelegate));
    TaggedStatChanged.Add(InStatTag, Created);
    return Handle;
}

/**
 * @brief 订阅指定属性的变化（绑定 Owner 生命周期版，内部采用 CreateWeakLambda 自动失效）
 * @param InOwner 归属对象（为空时按普通 Lambda 绑定；非空且失效时拒绝订阅并返回无效句柄）
 * @param InStatTag 目标属性标签
 * @param InCallback 变化回调闭包
 * @return 订阅句柄；参数非法或 Owner 已失效时返回无效句柄
 */
FDelegateHandle FGameStatSubscriptionHub::Subscribe(
    UObject* InOwner,
    const FGameplayTag& InStatTag,
    TFunction<void(const FGameplayTag&, float, float, float)> InCallback
) {
    if (!InStatTag.IsValid() || !InCallback) {
        return FDelegateHandle();
    }

    if (InOwner != nullptr) {
        if (!IsValid(InOwner)) {
            return FDelegateHandle();
        }
        return Subscribe(
            InStatTag,
            FOnGameStatChangedNative::FDelegate::CreateWeakLambda(InOwner, MoveTemp(InCallback))
        );
    }

    return Subscribe(InStatTag, FOnGameStatChangedNative::FDelegate::CreateLambda(MoveTemp(InCallback)));
}

/**
 * @brief 注销指定属性订阅（回调重入期间调用绝对安全）
 * @param InStatTag 目标属性标签
 * @param InHandle 订阅句柄
 */
void FGameStatSubscriptionHub::Unsubscribe(const FGameplayTag& InStatTag, FDelegateHandle InHandle) {
    if (!InStatTag.IsValid() || !InHandle.IsValid()) {
        return;
    }

    if (TSharedRef<FOnGameStatChangedNative>* Multicast = TaggedStatChanged.Find(InStatTag)) {
        (*Multicast)->Remove(InHandle);
        if (!(*Multicast)->IsBound()) {
            TaggedStatChanged.Remove(InStatTag);
        }
    }
}

/**
 * @brief 向指定属性的订阅者广播变化事件（共享引用保活，免疫回调内退订/订阅/重置重入）
 * @param InStatTag 目标属性标签
 * @param OldValue 旧值
 * @param NewValue 新值
 * @param MaxValue 上限值
 */
void FGameStatSubscriptionHub::Broadcast(const FGameplayTag& InStatTag, float OldValue, float NewValue, float MaxValue) {
    const TSharedRef<FOnGameStatChangedNative>* Found = TaggedStatChanged.Find(InStatTag);
    if (Found == nullptr) {
        return;
    }

    const TSharedRef<FOnGameStatChangedNative> LiveMulticast = *Found;
    LiveMulticast->Broadcast(InStatTag, OldValue, NewValue, MaxValue);

    if (!LiveMulticast->IsBound()) {
        if (const TSharedRef<FOnGameStatChangedNative>* Current = TaggedStatChanged.Find(InStatTag)) {
            if (&Current->Get() == &LiveMulticast.Get()) {
                TaggedStatChanged.Remove(InStatTag);
            }
        }
    }
}

/**
 * @brief 清除指定属性标签下的全部订阅者
 * @param InStatTag 目标属性标签
 */
void FGameStatSubscriptionHub::ClearTagSubscriptions(const FGameplayTag& InStatTag) {
    if (!InStatTag.IsValid()) {
        return;
    }
    TaggedStatChanged.Remove(InStatTag);
}

/**
 * @brief 重置全部订阅
 */
void FGameStatSubscriptionHub::Reset() {
    TaggedStatChanged.Reset();
}
