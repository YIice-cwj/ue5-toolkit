# GameGameplay 快速上手指南

本指南将带您在角色中集成 `GameGameplay` 模块，配置完整的 GAS 运行环境。

---

## 1. 角色类集成

### 头文件 `HeroCharacter.h`
```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "Damage/IDamageable.h"
#include "HeroCharacter.generated.h"

class UGameAbilitySystemComponent;
class UGameAbilitySystemBootstrap;
class UMyHeroAttributeSet;

UCLASS()
class AHeroCharacter : public ACharacter, public IAbilitySystemInterface, public IDamageable {
    GENERATED_BODY()

public:
    AHeroCharacter();

    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    virtual FGameDamageResult ApplyDamageContext_Implementation(const FGameDamageContext& Context) override;

protected:
    virtual void PossessedBy(AController* NewController) override;
    virtual void OnRep_PlayerState() override;

private:
    void InitializeGAS();

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UGameAbilitySystemComponent> AbilitySystemComponent;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UGameAbilitySystemBootstrap> BootstrapComponent;

    UPROPERTY()
    TObjectPtr<UMyHeroAttributeSet> AttributeSet;
};
```

---

## 2. 编写握手与伤害实现

### 源文件 `HeroCharacter.cpp`
```cpp
#include "HeroCharacter.h"
#include "Ability/UGameAbilitySystemComponent.h"
#include "Ability/UGameAbilitySystemBootstrap.h"
#include "MyHeroAttributeSet.h"

AHeroCharacter::AHeroCharacter()
{
    AbilitySystemComponent = CreateDefaultSubobject<UGameAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
    BootstrapComponent = CreateDefaultSubobject<UGameAbilitySystemBootstrap>(TEXT("BootstrapComponent"));
}

UAbilitySystemComponent* AHeroCharacter::GetAbilitySystemComponent() const
{
    return AbilitySystemComponent;
}

void AHeroCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);
    InitializeGAS();
}

void AHeroCharacter::OnRep_PlayerState()
{
    Super::OnRep_PlayerState();
    InitializeGAS();
}

void AHeroCharacter::InitializeGAS()
{
    // 注入解析 ASC 委托
    BootstrapComponent->SetResolveAbilitySystemComponentDelegate(FResolveAbilitySystemComponent::CreateUObject(this, &AHeroCharacter::GetAbilitySystemComponent));

    // 注入属性初始化委托
    BootstrapComponent->SetWriteInitialAttributesDelegate(FWriteInitialAttributes::CreateLambda([this](UAbilitySystemComponent* ASC) {
        if (!this->AttributeSet) {
            this->AttributeSet = NewObject<UMyHeroAttributeSet>(this, TEXT("AttributeSet"));
            ASC->AddAttributeSetSubobject(this->AttributeSet);
        }
    }));

    // 驱动握手与初始化（内部保障权威端/客户端分流、ASC变更自愈与状态幂等）
    BootstrapComponent->EnsureInitialized(this);
}

FGameDamageResult AHeroCharacter::ApplyDamageContext_Implementation(const FGameDamageContext& Context)
{
    // 构造或应用 Damage GameplayEffect，结算后同步返回结果
    FGameDamageResult Result;
    // 详细伤害实现请见 DamageExecutionGuide.md
    return Result;
}
```

