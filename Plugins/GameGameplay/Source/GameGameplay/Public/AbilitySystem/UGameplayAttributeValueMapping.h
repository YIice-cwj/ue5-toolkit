/**
 * @file UGameplayAttributeValueMapping.h
 * @brief 属性表现契约工具（ASC 属性变更委托 → 蓝图可订阅委托）
 *
 * 设计思路：把 ASC 的原生属性变更委托收敛为一个蓝图可订阅的 OnValueChanged（新值 / 旧值），
 * UI（UMG）订阅即可，游戏层无需感知属性复制细节；支持绑定时立即广播初值，消除 UI 初始空白；
 * 绑定持有 ASC 弱引用并保存委托句柄，销毁时自动解绑，杜绝悬垂与重复绑定。
 */

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "GameplayEffectTypes.h"
#include "UObject/Object.h"

#include "GameGameplay.h"
#include "UGameplayAttributeValueMapping.generated.h"

class UAbilitySystemComponent;

/**
 * @brief 属性值变更广播（UI 订阅：新值 / 旧值）
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMappedAttributeChanged, float, NewValue, float, OldValue);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMappedAttributeChangedNative, float, float);

/**
 * @brief 属性表现契约工具（游戏无关）
 */
UCLASS(BlueprintType)
class GAMEGAMEPLAY_API UGameplayAttributeValueMapping : public UObject {
    GENERATED_BODY()

public:
    /** 属性值变更广播（UI 订阅，游戏层无需感知复制细节） */
    UPROPERTY(BlueprintAssignable, Category = "Game|GAS")
    FOnMappedAttributeChanged OnValueChanged;

    /** 原生 C++ 属性值变更广播（支持 Lambda / 零反射开销） */
    FOnMappedAttributeChangedNative OnValueChangedNative;

    /**
     * @brief 静态工厂：创建并绑定指定属性变更监听对象
     * @param Outer 宿主对象（蓝图默认 Self，如 Widget 自身；为空则以 InAbilitySystemComponent 为宿主）
     * @param InAbilitySystemComponent 目标 ASC
     * @param InAttribute 目标属性
     * @param bBroadcastImmediately 是否在绑定成功后立即以当前值广播一次
     * @return 映射对象
     */
    UFUNCTION(BlueprintCallable, Category = "Game|GAS", meta = (DefaultToSelf = "Outer"))
    static UGameplayAttributeValueMapping* CreateAttributeValueMapping(
        UObject* Outer,
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayAttribute& InAttribute,
        bool bBroadcastImmediately = true
    );

    /**
     * @brief C++ 重载便利方法（以 InAbilitySystemComponent 为默认 Outer）
     */
    static UGameplayAttributeValueMapping* CreateAttributeValueMapping(
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayAttribute& InAttribute,
        bool bBroadcastImmediately = true
    ) {
        return CreateAttributeValueMapping(nullptr, InAbilitySystemComponent, InAttribute, bBroadcastImmediately);
    }

    void BeginDestroy() override;

    /**
     * @brief 绑定 ASC 的指定属性变更（重复绑定先解绑旧源，任一时刻只跟踪一个属性）
     * @param InAbilitySystemComponent 目标 ASC
     * @param InAttribute 目标属性
     * @param bBroadcastImmediately 是否在绑定成功后立即以当前值广播一次
     */
    UFUNCTION(BlueprintCallable, Category = "Game|GAS")
    void Bind(UAbilitySystemComponent* InAbilitySystemComponent, const FGameplayAttribute& InAttribute, bool bBroadcastImmediately = true);

    /**
     * @brief 解绑当前属性变更（幂等，可重复调用；对象销毁时自动调用）
     */
    UFUNCTION(BlueprintCallable, Category = "Game|GAS")
    void Unbind();

    /**
     * @brief 获取当前绑定的属性
     * @return 绑定的 GameplayAttribute
     */
    UFUNCTION(BlueprintPure, Category = "Game|GAS")
    [[nodiscard]] const FGameplayAttribute& GetBoundAttribute() const noexcept {
        return BoundAttribute;
    }

    /**
     * @brief 检查当前是否已绑定有效属性监听
     * @return 已绑定返回 true
     */
    UFUNCTION(BlueprintPure, Category = "Game|GAS")
    [[nodiscard]] bool IsBound() const noexcept {
        return AttributeChangedHandle.IsValid();
    }

    /**
     * @brief 获取当前绑定的属性数值
     * @return 当前属性数值，若未绑定或 ASC 无效返回 0.0f
     */
    UFUNCTION(BlueprintPure, Category = "Game|GAS")
    [[nodiscard]] float GetCurrentValue() const;

    /**
     * @brief 主动拉取当前数值并触发一次 OnValueChanged 广播
     */
    UFUNCTION(BlueprintCallable, Category = "Game|GAS")
    void QueryAndBroadcast();

private:
    void HandleAttributeChanged(const FOnAttributeChangeData& InChangeData);

    TWeakObjectPtr<UAbilitySystemComponent> BoundAbilitySystemComponent;
    FGameplayAttribute BoundAttribute;
    FDelegateHandle AttributeChangedHandle;
};
