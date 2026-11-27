/**
 * @file FGameStatSubscriptionHub.h
 * @brief 按 Tag 订阅管理中心（单一职责 · 负责重入安全广播与 Owner 弱引用生命周期自动感知）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Stat/FGameStatTypes.h"

/**
 * @brief 按 Tag 订阅中心（共享保活防重入 UAF + UE 原生 WeakLambda 弱绑定零额外扫描开销）
 */
struct GAMEGAMEPLAYCORE_API FGameStatSubscriptionHub {
public:
    FGameStatSubscriptionHub() = default;

    /**
     * @brief 订阅指定属性的变化（底层委托版）
     * @param InStatTag 目标属性标签
     * @param InDelegate 变化回调委托
     * @return 订阅句柄；标签或委托无效时返回无效句柄
     */
    FDelegateHandle Subscribe(const FGameplayTag& InStatTag, FOnGameStatChangedNative::FDelegate InDelegate);

    /**
     * @brief 订阅指定属性的变化（绑定 Owner 生命周期版，内部采用 CreateWeakLambda 自动失效）
     * @param InOwner 归属对象（为空时按普通 Lambda 绑定；非空且失效时拒绝订阅并返回无效句柄）
     * @param InStatTag 目标属性标签
     * @param InCallback 变化回调闭包
     * @return 订阅句柄；参数非法或 Owner 已失效时返回无效句柄
     */
    FDelegateHandle Subscribe(
        UObject* InOwner,
        const FGameplayTag& InStatTag,
        TFunction<void(const FGameplayTag&, float, float, float)> InCallback
    );

    /**
     * @brief 注销指定属性订阅（回调重入期间调用绝对安全）
     * @param InStatTag 目标属性标签
     * @param InHandle 订阅句柄
     */
    void Unsubscribe(const FGameplayTag& InStatTag, FDelegateHandle InHandle);

    /**
     * @brief 向指定属性的订阅者广播变化事件（共享引用保活，免疫回调内退订/订阅/重置重入）
     * @param InStatTag 目标属性标签
     * @param OldValue 旧值
     * @param NewValue 新值
     * @param MaxValue 上限值
     */
    void Broadcast(const FGameplayTag& InStatTag, float OldValue, float NewValue, float MaxValue);

    /**
     * @brief 清除指定属性标签下的全部订阅者
     * @param InStatTag 目标属性标签
     */
    void ClearTagSubscriptions(const FGameplayTag& InStatTag);

    /**
     * @brief 重置全部订阅
     */
    void Reset();

private:
    TMap<FGameplayTag, TSharedRef<FOnGameStatChangedNative>> TaggedStatChanged;
};
