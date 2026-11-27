# GameGameplay 伤害通道与 ExecCalc 实战指南

本指南展示如何继承 `UGameDamageExecution` 实现包含元素克制、防御减免与暴击反馈的自定义结算类。

---

## 1. 声明并注册通道映射

```cpp
#include "Damage/UGameDamageExecution.h"
#include "MyHeroAttributeSet.h"
#include "MyCombatDamageExecution.generated.h"

UCLASS()
class UMyCombatDamageExecution : public UGameDamageExecution {
    GENERATED_BODY()

public:
    UMyCombatDamageExecution() {
        RegisterChannelBinding(
            FGameplayTag::RequestGameplayTag(TEXT("Damage.Physical")),
            UMyHeroAttributeSet::GetPhysicalDamageAttribute(),
            EGameplayModOp::Additive
        );

        RegisterChannelBinding(
            FGameplayTag::RequestGameplayTag(TEXT("Damage.Fire")),
            UMyHeroAttributeSet::GetSpellDamageAttribute(),
            EGameplayModOp::Additive
        );
    }

protected:
    FDamageModifier ComputeChannelModifier(
        const FGameplayTag& ChannelTag,
        float BaseDamage,
        const FGameplayTagContainer& SourceTags,
        const FGameplayTagContainer& TargetTags,
        const FGameplayEffectCustomExecutionParameters& ExecutionParams
    ) const override {
        FDamageModifier Modifier;
        if (TargetTags.HasTag(FGameplayTag::RequestGameplayTag(TEXT("State.Vulnerable")))) {
            Modifier.Multiplier *= 1.3f;
        }
        return Modifier;
    }

    void OnPostExecuteChannels(
        const FGameplayEffectCustomExecutionParameters& ExecutionParams,
        float TotalCalculatedDamage,
        FGameGameplayEffectContext* OutEffectContext
    ) const override {
        if (OutEffectContext != nullptr && TotalCalculatedDamage >= 100.0f) {
            OutEffectContext->AddFeedbackTag(FGameplayTag::RequestGameplayTag(TEXT("Damage.Feedback.HeavyHit")));
            OutEffectContext->SetDynamicValue(FGameplayTag::RequestGameplayTag(TEXT("Damage.Feedback.HitStop")), 0.15f);
        }
    }
};
```

运行期动态增删通道统一通过权威注册表 `FGameDamageChannelRegistry::Get()` 操作（线程安全，且允许在回调中安全重入）：

```cpp
#include "Damage/FGameDamageChannelRegistry.h"

auto& Registry = FGameDamageChannelRegistry::Get();
Registry.Register(
    UMyCombatDamageExecution::StaticClass(),
    FDamageChannelBinding(
        FGameplayTag::RequestGameplayTag(TEXT("Damage.Ice")),
        UMyHeroAttributeSet::GetSpellDamageAttribute()
    )
);

Registry.Unregister(
    UMyCombatDamageExecution::StaticClass(),
    FGameplayTag::RequestGameplayTag(TEXT("Damage.Ice"))
);
```

---

## 2. 在 GameplayEffect 蓝图中配置

1. 打开或创建 `GE_Damage`；
2. 在 `Executions` 数组中添加一项，将 `Calculation Class` 设为 `UMyCombatDamageExecution`；
3. 确保项目 `Config/DefaultGame.ini` 已将 `AbilitySystemGlobalsClassName` 指向 `/Script/GameGameplay.GameAbilitySystemGlobals`；
4. 当技能施加携带 SetByCaller 通道数值的 GE 时，各通道将自动修饰结算并回写打击反馈上下文。
