# GameGameplay 伤害通道与 ExecCalc 实战指南

本指南展示如何继承 `UGameDamageExecution` 实现包含护甲减免、元素克制和暴击计算的自定义结算类。

---

## 1. 声明并注册通道映射

创建子类 `UMyCombatDamageExecution`并在构造函数中绑定通道：

```cpp
#include "Damage/UGameDamageExecution.h"
#include "MyHeroAttributeSet.h"
#include "MyCombatDamageExecution.generated.h"

UCLASS()
class UMyCombatDamageExecution : public UGameDamageExecution {
    GENERATED_BODY()

public:
    UMyCombatDamageExecution() {
        // 1. 物理伤害映射到 PhysicalDamage 元属性
        RegisterChannelBinding(
            FGameplayTag::RequestGameplayTag(TEXT("Damage.Physical")),
            UMyHeroAttributeSet::GetPhysicalDamageAttribute(),
            EGameplayModOp::Additive
        );

        // 2. 元素火焰伤害映射到 SpellDamage 元属性
        RegisterChannelBinding(
            FGameplayTag::RequestGameplayTag(TEXT("Damage.Fire")),
            UMyHeroAttributeSet::GetSpellDamageAttribute(),
            EGameplayModOp::Additive
        );
    }

protected:
    virtual FDamageModifier ComputeChannelModifier(
        const FGameplayTag& ChannelTag,
        float BaseDamage,
        const FGameplayTagContainer& SourceTags,
        const FGameplayTagContainer& TargetTags,
        const FGameplayEffectCustomExecutionParameters& ExecutionParams
    ) const override {
        FDamageModifier Modifier;

        // 根据受击者标签判断是否有虚弱/易伤
        if (TargetTags.HasTag(FGameplayTag::RequestGameplayTag(TEXT("State.Vulnerable")))) {
            Modifier.Multiplier *= 1.3f; // 易伤增伤 30%
        }

        return Modifier;
    }

    virtual void OnPostExecuteChannels(
        const FGameplayEffectCustomExecutionParameters& ExecutionParams,
        float TotalCalculatedDamage,
        FGameGameplayEffectContext* OutEffectContext
    ) const override {
        // 自定义打击反馈：例如高额伤害触发重击与卡肉
        if (OutEffectContext != nullptr && TotalCalculatedDamage >= 100.0f) {
            OutEffectContext->AddFeedbackTag(FGameplayTag::RequestGameplayTag(TEXT("Damage.Feedback.HeavyHit")));
            OutEffectContext->SetDynamicValue(FGameplayTag::RequestGameplayTag(TEXT("Damage.Feedback.HitStop")), 0.15f);
        }
    }
};
```

构造函数注册与运行期注册等价：绑定统一写入 `FGameDamageChannelRegistry`。需要在启动流程、模组加载或技能配置变更时动态增删通道，可直接调用静态接口：

```cpp
UGameDamageExecution::RegisterRuntimeChannelBinding(
    UMyCombatDamageExecution::StaticClass(),
    FGameplayTag::RequestGameplayTag(TEXT("Damage.Ice")),
    UMyHeroAttributeSet::GetSpellDamageAttribute());

UGameDamageExecution::UnregisterRuntimeChannelBinding(
    UMyCombatDamageExecution::StaticClass(),
    FGameplayTag::RequestGameplayTag(TEXT("Damage.Ice")));
```

注册表按 ExecCalc 类隔离、线程安全；查询时沿继承链合并，派生类同名通道优先，避免重复结算。

---

## 2. 在 GameplayEffect 蓝图中配置

1. 打开或创建 `GE_Damage`；
2. 在 `Executions` 数组中添加一项，将 `Calculation Class` 设为 `UMyCombatDamageExecution`；
3. 将该 GE 的 Context 指向 `FGameGameplayEffectContext`；
4. 当技能施加该 GE 时，物理和元素通道将分别计算修饰并自动累加至角色属性集中。

