#include "Attribute/UGameplayAttributeValueMapping.h"

#include "AbilitySystemComponent.h"

UGameplayAttributeValueMapping* UGameplayAttributeValueMapping::CreateAttributeValueMapping(
    UObject* Outer,
    UAbilitySystemComponent* InAbilitySystemComponent,
    const FGameplayAttribute& InAttribute,
    bool bBroadcastImmediately
) {
    if (InAbilitySystemComponent == nullptr) {
        return nullptr;
    }

    UObject* EffectiveOuter = Outer != nullptr ? Outer : static_cast<UObject*>(InAbilitySystemComponent);
    UGameplayAttributeValueMapping* Mapping = NewObject<UGameplayAttributeValueMapping>(EffectiveOuter);
    Mapping->Bind(InAbilitySystemComponent, InAttribute, bBroadcastImmediately);
    return Mapping;
}

void UGameplayAttributeValueMapping::BeginDestroy() {
    Unbind();
    Super::BeginDestroy();
}

void UGameplayAttributeValueMapping::Bind(
    UAbilitySystemComponent* InAbilitySystemComponent,
    const FGameplayAttribute& InAttribute,
    bool bBroadcastImmediately
) {
    if (InAbilitySystemComponent == nullptr) {
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

float UGameplayAttributeValueMapping::GetCurrentValue() const {
    const UAbilitySystemComponent* AbilitySystemComponent = BoundAbilitySystemComponent.Get();
    if (AbilitySystemComponent == nullptr || !BoundAttribute.IsValid()) {
        return 0.0f;
    }
    return AbilitySystemComponent->GetNumericAttribute(BoundAttribute);
}

void UGameplayAttributeValueMapping::QueryAndBroadcast() {
    const float CurrentVal = GetCurrentValue();
    OnValueChanged.Broadcast(CurrentVal, CurrentVal);
    OnValueChangedNative.Broadcast(CurrentVal, CurrentVal);
}

void UGameplayAttributeValueMapping::HandleAttributeChanged(const FOnAttributeChangeData& InChangeData) {
    OnValueChanged.Broadcast(InChangeData.NewValue, InChangeData.OldValue);
    OnValueChangedNative.Broadcast(InChangeData.NewValue, InChangeData.OldValue);
}
