/**
 * @file UGameplayAttributeValueMapping.cpp
 * @brief 属性表现契约工具实现（ASC 属性变更委托 -> 蓝图可订阅委托）
 */

#include "Attribute/UGameplayAttributeValueMapping.h"

#include "AbilitySystemComponent.h"

/**
 * @brief 对象销毁生命周期回调：解绑当前属性监听，杜绝悬垂委托
 */
void UGameplayAttributeValueMapping::BeginDestroy() {
    Unbind();
    Super::BeginDestroy();
}

/**
 * @brief 绑定 ASC 的指定属性变更（重复绑定先解绑旧源，任一时刻只跟踪一个属性）
 * @param InAbilitySystemComponent 目标 ASC
 * @param InAttribute 目标属性
 * @param bBroadcastImmediately 是否在绑定成功后立即以当前值广播一次
 */
void UGameplayAttributeValueMapping::Bind(
    UAbilitySystemComponent* InAbilitySystemComponent,
    const FGameplayAttribute& InAttribute,
    bool bBroadcastImmediately
) {
    if (InAbilitySystemComponent == nullptr || !InAttribute.IsValid()) {
        return;
    }

    Unbind();
    BoundAbilitySystemComponent = InAbilitySystemComponent;
    BoundAttribute = InAttribute;
    AttributeChangedHandle = InAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(InAttribute).AddUObject(this, &UGameplayAttributeValueMapping::HandleAttributeChanged);

    if (bBroadcastImmediately) {
        QueryAndBroadcast();
    }
}

/**
 * @brief 解绑当前属性变更（幂等，可重复调用；对象销毁时自动调用）
 */
void UGameplayAttributeValueMapping::Unbind() {
    if (AttributeChangedHandle.IsValid()) {
        if (UAbilitySystemComponent* AbilitySystemComponent = BoundAbilitySystemComponent.Get()) {
            if (IsValid(AbilitySystemComponent) && !AbilitySystemComponent->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) {
                AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(BoundAttribute).Remove(AttributeChangedHandle);
            }
        }
        AttributeChangedHandle.Reset();
    }
    BoundAttribute = FGameplayAttribute();
    BoundAbilitySystemComponent.Reset();
}

/**
 * @brief 获取当前绑定的属性数值
 * @return 当前属性数值，若未绑定或 ASC 无效返回 0.0f
 */
float UGameplayAttributeValueMapping::GetCurrentValue() const {
    const UAbilitySystemComponent* AbilitySystemComponent = BoundAbilitySystemComponent.Get();
    if (AbilitySystemComponent == nullptr || !BoundAttribute.IsValid()) {
        return 0.0f;
    }
    return AbilitySystemComponent->GetNumericAttribute(BoundAttribute);
}

/**
 * @brief 主动拉取当前数值并触发一次 OnValueChanged 广播
 */
void UGameplayAttributeValueMapping::QueryAndBroadcast() {
    const float CurrentVal = GetCurrentValue();
    OnValueChanged.Broadcast(CurrentVal, CurrentVal);
    OnValueChangedNative.Broadcast(CurrentVal, CurrentVal);
}

/**
 * @brief 静态工厂：创建并绑定指定属性变更监听对象
 * @param Outer 宿主对象
 * @param InAbilitySystemComponent 目标 ASC
 * @param InAttribute 目标属性
 * @param bBroadcastImmediately 是否在绑定成功后立即以当前值广播一次
 * @return 映射对象指针
 */
UGameplayAttributeValueMapping* UGameplayAttributeValueMapping::CreateAttributeValueMapping(
    UObject* Outer,
    UAbilitySystemComponent* InAbilitySystemComponent,
    const FGameplayAttribute& InAttribute,
    bool bBroadcastImmediately
) {
    if (InAbilitySystemComponent == nullptr || !InAttribute.IsValid()) {
        return nullptr;
    }

    UObject* EffectiveOuter = Outer != nullptr ? Outer : static_cast<UObject*>(InAbilitySystemComponent);
    UGameplayAttributeValueMapping* Mapping = NewObject<UGameplayAttributeValueMapping>(EffectiveOuter);
    Mapping->Bind(InAbilitySystemComponent, InAttribute, bBroadcastImmediately);
    return Mapping;
}

/**
 * @brief 将 ASC 属性变更数据转发给蓝图与原生订阅方
 * @param InChangeData ASC 提供的属性新旧值变更数据
 */
void UGameplayAttributeValueMapping::HandleAttributeChanged(const FOnAttributeChangeData& InChangeData) {
    OnValueChanged.Broadcast(InChangeData.NewValue, InChangeData.OldValue);
    OnValueChangedNative.Broadcast(InChangeData.NewValue, InChangeData.OldValue);
}
