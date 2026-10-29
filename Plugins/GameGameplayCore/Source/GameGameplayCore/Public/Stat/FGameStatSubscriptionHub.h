/**
 * @file FGameStatSubscriptionHub.h
 * @brief 按 Tag 订阅管理中心（单一职责 · 负责 Tag 细粒度订阅与生命周期弱引用管理）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Stat/FGameStatTypes.h"

/**
 * @brief 按 Tag 订阅中心
 */
struct GAMEGAMEPLAYCORE_API FGameStatSubscriptionHub {
public:
    FGameStatSubscriptionHub() = default;

    /**
     * @brief 订阅指定属性的变化（底层委托版）
     * @param InStatTag 目标属性标签
     * @param InDelegate 变化回调委托
     * @return 订阅句柄
     */
    FDelegateHandle Subscribe(const FGameplayTag& InStatTag, FOnGameStatChangedNative::FDelegate InDelegate);

    /**
     * @brief 订阅指定属性的变化（绑定 Owner 生命周期版）
     * @param InOwner 归属对象（失效时自动停止回调并适时清理）
     * @param InStatTag 目标属性标签
     * @param InCallback 变化回调
     * @return 订阅句柄
     */
    FDelegateHandle Subscribe(
        UObject* InOwner,
        const FGameplayTag& InStatTag,
        TFunction<void(const FGameplayTag&, float, float, float)> InCallback
    );

    /**
     * @brief 注销指定属性订阅
     * @param InStatTag 目标属性标签
     * @param InHandle 订阅句柄
     */
    void Unsubscribe(const FGameplayTag& InStatTag, FDelegateHandle InHandle);

    /**
     * @brief 向指定属性的订阅者广播变化事件
     * @param InStatTag 目标属性标签
     * @param OldValue 旧值
     * @param NewValue 新值
     * @param MaxValue 上限值
     */
    void Broadcast(const FGameplayTag& InStatTag, float OldValue, float NewValue, float MaxValue);

    /**
     * @brief 清理指定属性中已失效 Owner 的订阅条目
     * @param InStatTag 目标属性标签
     */
    void PurgeExpiredOwnerSubscriptions(const FGameplayTag& InStatTag);

    /**
     * @brief 重置全部订阅
     */
    void Reset();

private:
    struct FStatOwnerSubscription {
        FGameplayTag Tag;
        TWeakObjectPtr<UObject> Owner;
        FDelegateHandle Handle;
    };

    TMap<FGameplayTag, FOnGameStatChangedNative> TaggedStatChanged;
    TArray<FStatOwnerSubscription> OwnerStatSubscriptions;
};
