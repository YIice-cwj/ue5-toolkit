# GameGameplayCore 快速上手指南

本指南展示如何从零开始，为一个普通敌人 Actor 添加受击判定与通用数值组件，并让玩家攻击该敌人。

---

## 步骤 1：在目标模块添加依赖

在 `MyGame.Build.cs` 中添加：
```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core",
    "CoreUObject",
    "Engine",
    "GameplayTags",
    "GameCore",
    "GameGameplayCore", // 引入战斗核心契约
});
```

---

## 步骤 2：创建受击敌人 `ADummyTarget`

### 头文件 `DummyTarget.h`
```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Damage/IDamageable.h"
#include "Stat/UGameStatComponent.h"
#include "DummyTarget.generated.h"

UCLASS()
class ADummyTarget : public AActor, public IDamageable {
    GENERATED_BODY()

public:
    ADummyTarget();

    virtual FGameDamageResult ApplyDamageContext_Implementation(const FGameDamageContext& Context) override;

protected:
    virtual void BeginPlay() override;

private:
    UFUNCTION()
    void HandleVitalStateChanged(bool bIsAlive, const FGameplayTag& VitalTag);

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UGameStatComponent> StatComponent;
};
```

### 源文件 `DummyTarget.cpp`
```cpp
#include "DummyTarget.h"

ADummyTarget::ADummyTarget()
{
    StatComponent = CreateDefaultSubobject<UGameStatComponent>(TEXT("StatComponent"));

    // 开发者自由注册所需属性
    const FGameplayTag HealthTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Health"));
    StatComponent->RegisterStat(HealthTag, 100.0f, 100.0f, 0.0f);
    StatComponent->SetPrimaryVitalTag(HealthTag);
}

void ADummyTarget::BeginPlay()
{
    Super::BeginPlay();
    StatComponent->OnVitalStateChanged.AddDynamic(this, &ADummyTarget::HandleVitalStateChanged);
}

FGameDamageResult ADummyTarget::ApplyDamageContext_Implementation(const FGameDamageContext& Context)
{
    const FGameplayTag HealthTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Health"));
    const float IncomingDamage = Context.GetTotalBaseDamage();
    float ActualDelta = 0.0f;

    // 扣减生命属性并获取闭环结算差值
    StatComponent->ModifyStat(HealthTag, -IncomingDamage, ActualDelta);

    const bool bWasFatal = !StatComponent->IsVitalAlive();

    // 组装反馈包
    FGameDamageResult Result;
    Result.SetActualDamage(FMath::Abs(ActualDelta));
    Result.AddFeedbackTag(FGameplayTag::RequestGameplayTag(TEXT("Damage.Feedback.Hit")));
    if (bWasFatal)
    {
        Result.AddFeedbackTag(FGameplayTag::RequestGameplayTag(TEXT("Damage.Feedback.Fatal")));
    }
    
    // 注入动态卡肉数值（击杀时卡肉更久）
    Result.SetDynamicValue(
        FGameplayTag::RequestGameplayTag(TEXT("Damage.Feedback.HitStop")),
        bWasFatal ? 0.12f : 0.05f
    );

    return Result;
}

void ADummyTarget::HandleVitalStateChanged(bool bIsAlive, const FGameplayTag& VitalTag)
{
    if (!bIsAlive)
    {
        // 播放死亡动画或销毁
        SetLifeSpan(2.0f);
    }
}
```

---

## 步骤 3：攻击者分发伤害并处理反馈

在玩家武器或攻击技能中：

```cpp
void AWeapon::OnHitTarget(AActor* TargetActor)
{
    if (TargetActor && TargetActor->Implements<UDamageable>())
    {
        FGameDamageContext Context;
        Context.Instigator = GetInstigator();
        Context.DamageCauser = this;
        Context.AddChannelDamage(FGameplayTag::RequestGameplayTag(TEXT("Damage.Physical")), 35.0f);

        // 同步分发并即时获得闭环反馈
        FGameDamageResult Result = IDamageable::Execute_ApplyDamageContext(TargetActor, Context);

        // 攻击者直接驱动打击感（如卡肉与击杀震屏）
        const float HitStop = Result.GetDynamicValue(FGameplayTag::RequestGameplayTag(TEXT("Damage.Feedback.HitStop")));
        if (HitStop > 0.0f)
        {
            ApplyHitStopToAttacker(HitStop);
        }

        if (Result.HasFeedbackTag(FGameplayTag::RequestGameplayTag(TEXT("Damage.Feedback.Fatal"))))
        {
            PlayKillConfirmedSound();
        }
    }
}
```
