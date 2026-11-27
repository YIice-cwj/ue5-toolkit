/**
 * @file UGameplayAttributeValueMapping.h
 * @brief 属性表现契约工具（ASC 属性变更委托 → 蓝图与原生可订阅委托，支持对象池复用）
 *
 * 设计思路：把 ASC 的原生属性变更委托收敛为一个蓝图可订阅的 OnValueChanged（新值 / 旧值）
 * 与零反射开销的原生委托 OnValueChangedNative；工厂创建返回映射对象后由调用方完成订阅，
 * 再显式调用 QueryAndBroadcast 拉取初值，消除创建期空广播反模式；支持 IGamePoolable 对象池复用。
 */

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "Core/IGamePoolable.h"
#include "GameplayEffectTypes.h"
#include "UObject/Object.h"

#include "UGameplayAttributeValueMapping.generated.h"

class UAbilitySystemComponent;

/**
 * @brief 属性值变更广播（UI 蓝图订阅：新值 / 旧值）
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMappedAttributeChanged, float, NewValue, float, OldValue);

/**
 * @brief 属性值变更原生广播（C++ 订阅：新值 / 旧值，零反射开销）
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMappedAttributeChangedNative, float, float);

/**
 * @brief 属性表现契约工具（游戏无关，支持对象池安全复用）
 */
UCLASS(BlueprintType)
class GAMEGAMEPLAY_API UGameplayAttributeValueMapping : public UObject, public IGamePoolable {
    GENERATED_BODY()

public:
    /**
     * @brief 属性值变更广播动态多播委托（UI 订阅，游戏层无需感知复制细节）
     */
    UPROPERTY(BlueprintAssignable, Category = "Game|GAS")
    FOnMappedAttributeChanged OnValueChanged;

    /**
     * @brief 原生 C++ 属性值变更广播委托（支持 Lambda 闭包 / 零反射开销）
     */
    FOnMappedAttributeChangedNative OnValueChangedNative;

    /**
     * @brief 静态工厂：创建并绑定指定属性变更监听对象（调用方订阅委托后调用 QueryAndBroadcast 拉取初值）
     * @param Outer 宿主对象（蓝图默认 Self，如 Widget 自身；为空则以 InAbilitySystemComponent 为宿主）
     * @param InAbilitySystemComponent 目标 ASC
     * @param InAttribute 目标属性
     * @return 映射对象；ASC 为空或属性无效时返回 nullptr
     */
    UFUNCTION(BlueprintCallable, Category = "Game|GAS", meta = (DefaultToSelf = "Outer"))
    static UGameplayAttributeValueMapping* CreateAttributeValueMapping(
        UObject* Outer,
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayAttribute& InAttribute
    );

    /**
     * @brief C++ 便利重载（以 InAbilitySystemComponent 为默认 Outer）
     * @param InAbilitySystemComponent 目标 ASC
     * @param InAttribute 目标属性
     * @return 映射对象；ASC 为空或属性无效时返回 nullptr
     */
    static UGameplayAttributeValueMapping* CreateAttributeValueMapping(
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayAttribute& InAttribute
    ) {
        return CreateAttributeValueMapping(nullptr, InAbilitySystemComponent, InAttribute);
    }

    /**
     * @brief 对象销毁生命周期回调：解绑当前属性监听，杜绝悬垂委托
     */
    void BeginDestroy() override;

    /**
     * @brief 从对象池出库激活回调（保持洁净待绑定态）
     */
    void OnAcquireFromPool_Implementation() override;

    /**
     * @brief 归还对象池休眠回调：解绑 ASC 监听并清空所有蓝图与原生订阅者
     */
    void OnReleaseToPool_Implementation() override;

    /**
     * @brief 绑定 ASC 的指定属性变更（有效参数先解绑旧源再重绑；无效参数直接忽略并保留原绑定）
     * @param InAbilitySystemComponent 目标 ASC
     * @param InAttribute 目标属性
     * @param bBroadcastImmediately 是否在绑定成功后立即以当前值广播一次（适合已有订阅者的重绑场景）
     */
    UFUNCTION(BlueprintCallable, Category = "Game|GAS")
    void Bind(UAbilitySystemComponent* InAbilitySystemComponent, const FGameplayAttribute& InAttribute, bool bBroadcastImmediately = true);

    /**
     * @brief 解绑当前属性变更（幂等，可重复调用；对象销毁或入池时自动调用）
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
     * @brief 获取当前绑定的 AbilitySystemComponent
     * @return 绑定的 ASC 指针；若未绑定或已失效返回 nullptr
     */
    UFUNCTION(BlueprintPure, Category = "Game|GAS")
    [[nodiscard]] UAbilitySystemComponent* GetBoundAbilitySystemComponent() const noexcept {
        return BoundAbilitySystemComponent.Get();
    }

    /**
     * @brief 检查当前是否已绑定且目标 ASC 依然有效
     * @return 委托句柄与 ASC 弱引用均有效返回 true
     */
    UFUNCTION(BlueprintPure, Category = "Game|GAS")
    [[nodiscard]] bool IsBound() const noexcept {
        return AttributeChangedHandle.IsValid() && BoundAbilitySystemComponent.IsValid();
    }

    /**
     * @brief 获取当前绑定的属性数值
     * @return 当前属性数值，若未绑定或 ASC 无效返回 0.0f
     */
    UFUNCTION(BlueprintPure, Category = "Game|GAS")
    [[nodiscard]] float GetCurrentValue() const;

    /**
     * @brief 主动拉取当前数值并触发一次 OnValueChanged 广播（未绑定或 ASC 已失效时静默跳过，避免向 UI 误播 0）
     */
    UFUNCTION(BlueprintCallable, Category = "Game|GAS")
    void QueryAndBroadcast();

private:
    /**
     * @brief 将 ASC 属性变更数据转发给蓝图与原生订阅方
     * @param InChangeData ASC 提供的属性新旧值变更数据
     */
    void HandleAttributeChanged(const FOnAttributeChangeData& InChangeData);

    TWeakObjectPtr<UAbilitySystemComponent> BoundAbilitySystemComponent;
    FGameplayAttribute BoundAttribute;
    FDelegateHandle AttributeChangedHandle;
};
