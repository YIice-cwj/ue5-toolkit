# GameGameplayCore 代理模式接入指南 (Proxy)

当角色已经拥有专属的属性系统（如 GAS AttributeSet、自定义 ECS 数值表、或外部网络同步属性源）时，如果让通用组件再单独存一份属性，必然会导致**双重状态同步、数据源撕裂、以及一方修改另一方未更新**的灾难。

`UGameStatComponent` 的 **Proxy 代理模式** 允许将组件自身的数值查询完全重定向到外部真实数据源，而组件仅负责统一的接口封装与变动广播。

---

## 1. 接入方式 A：实现 `IGameStatValueSource` 接口（蓝图与 C++ 通用）

### 步骤 1：让数据持有者实现接口
```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Combat/IGameStatValueSource.h"
#include "CustomHero.generated.h"

UCLASS()
class ACustomHero : public ACharacter, public IGameStatValueSource {
    GENERATED_BODY()

public:
    // --- 实现 IGameStatValueSource ---
    virtual float GetStatCurrent_Implementation(const FGameplayTag& InStatTag) const override {
        if (InStatTag.MatchesTagExact(TagHealth)) return CurrentCustomHealth;
        if (InStatTag.MatchesTagExact(TagShield)) return CurrentCustomShield;
        return 0.0f;
    }

    virtual float GetStatMax_Implementation(const FGameplayTag& InStatTag) const override {
        if (InStatTag.MatchesTagExact(TagHealth)) return MaxCustomHealth;
        if (InStatTag.MatchesTagExact(TagShield)) return MaxCustomShield;
        return 100.0f;
    }

private:
    float CurrentCustomHealth = 500.0f;
    float MaxCustomHealth = 500.0f;
    float CurrentCustomShield = 100.0f;
    float MaxCustomShield = 100.0f;
    FGameplayTag TagHealth;
    FGameplayTag TagShield;
};
```

### 步骤 2：在组件初始化时绑定代理源
```cpp
void ACustomHero::BeginPlay()
{
    Super::BeginPlay();

    // 绑定当前 Actor 作为数值源
    StatComponent->BindStatSource(this);
}
```

---

## 2. 接入方式 B：原生 C++ 委托直接绑定（极致性能，零虚函数开销）

对于对性能有极高要求的高频查询场景，可以直接注入原生 C++ 委托：

```cpp
void ACustomHero::SetupNativeProxy()
{
    StatComponent->BindNativeStatQueries(
        FGameStatQueryDelegate::CreateUObject(this, &ACustomHero::QueryCurrentStatFromAttributeSet),
        FGameStatQueryDelegate::CreateUObject(this, &ACustomHero::QueryMaxStatFromAttributeSet)
    );
}

float ACustomHero::QueryCurrentStatFromAttributeSet(const FGameplayTag& StatTag) const
{
    return MyAttributeSet->GetNumericAttributeByTag(StatTag);
}

float ACustomHero::QueryMaxStatFromAttributeSet(const FGameplayTag& StatTag) const
{
    return MyAttributeSet->GetMaxNumericAttributeByTag(StatTag);
}
```

---

## 3. 代理模式写入契约

代理模式默认只读：`ModifyStat` / `SetStatCurrent` / `SetStatMax` 不会直接改本地缓存，而是转发给显式绑定的写委托。

```cpp
void ACustomHero::SetupProxyWriter()
{
    StatComponent->BindNativeStatWriter(
        FGameStatWriteDelegate::CreateUObject(this, &ACustomHero::WriteStatToAttributeSet));
}

bool ACustomHero::WriteStatToAttributeSet(
    const FGameplayTag& StatTag, EGameStatWriteOp Op, float Value, float& OutAppliedDelta)
{
    return MyAttributeSet->ApplyStatWrite(StatTag, Op, Value, OutAppliedDelta);
}
```

- 写委托返回 `true` 表示外部源已生效，组件随后回读并广播实际变化；返回 `false` 时本次写操作失败。
- **未注册属性代理写闭环**：即使属性未在组件本地表注册（纯外部托管），外部写入生效后组件依然会评估修正器，并在派生当前值或派生上限变化时触发 `BroadcastStatChanged`；若属性匹配 `PrimaryVitalTag`，自动执行 `EvaluateVitalState` 判定生死并广播事件。
- **代理模式复活**：`ReviveVital(FillPercent)` 读取外部源的上限与本地注册的下限，按比例计算后经 `SetStatCurrent` 透传至写委托恢复数值。
- 未绑定写委托时，`ModifyStat` 返回 `false`，`SetStatCurrent` / `SetStatMax` 放弃写入，代理源保持权威。
- 非权威端（客户端）默认拒绝写入并记录 Warning；确需客户端预测时在类默认值中开启 `bAllowNonAuthoritativeWrites`。
- 可用 `CanWriteStat()` 在 UI 上提前置灰不可写操作。

---

## 4. 解绑与回退安全机制

当宿主需要临时解除外部代理（例如英雄脱离外装装甲或重构属性系统）并调用 `UnbindStatSource()` 时：
1. `UGameStatComponent` 会读取最后一次代理源返回的各属性实际数值，将其作为初始值安全写回组件内部表；
2. 依据当前数值重新判定主生命状态（若配置了 `PrimaryVitalTag` 且当前值归零则保持 Dead，否则保持 Alive），确保生命周期的严谨性。

