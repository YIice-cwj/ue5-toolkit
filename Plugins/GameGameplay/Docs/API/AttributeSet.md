# GameGameplay 属性集通用基类 API 参考

`UGameBaseAttributeSet` 定位于**纯粹通用机制基类（零具体业务属性假设）**：不硬编码任何具体属性或冗余钳制套壳（属性上下限钳制统一直接使用引擎原生 `FMath::Clamp`），为派生属性集提供阈值状态幂等闩、元属性归零提取与安全本地增量落地工具。

头文件：`#include "Attribute/UGameBaseAttributeSet.h"`  
基类：`UAttributeSet`

---

## 1. 类接口声明

```cpp
UCLASS(Abstract)
class GAMEGAMEPLAY_API UGameBaseAttributeSet : public UAttributeSet {
    GENERATED_BODY()

public:
    [[nodiscard]] static bool BroadcastThresholdOnce(
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayTag& InThresholdTag
    );

    static void ResetThreshold(
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayTag& InThresholdTag
    );

    [[nodiscard]] FORCEINLINE static float MoveMetaAttribute(FGameplayAttributeData& MetaAttributeData) noexcept {
        const float Value = MetaAttributeData.GetCurrentValue();
        MetaAttributeData.SetBaseValue(0.0f);
        MetaAttributeData.SetCurrentValue(0.0f);
        return Value;
    }

    static void ApplyAttributeToLocal(
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayAttribute& InTargetAttribute,
        float InDelta
    );
};
```

---

## 2. 静态实用工具方法说明

### 2.1 `BroadcastThresholdOnce` / `ResetThreshold`
- **职责**：阈值状态幂等闩与复位入口（如残血首次低于 30% 触发一次通知，恢复后复位）。
- **安全边界**：`InAbilitySystemComponent == nullptr` 或 `!InThresholdTag.IsValid()` 时直接短路返回 `false`/空操作，杜绝无效空标签污染 ASC。正常路径日志输出至 `LogGameAttribute`（`Verbose`）。

### 2.2 `MoveMetaAttribute`
- **职责**：提取元属性当前数值后立即将 BaseValue 与 CurrentValue 归零（头文件 `FORCEINLINE` 零调用开销），防止跨结算帧残留。

### 2.3 `ApplyAttributeToLocal`
- **职责**：收敛“读取旧 BaseValue -> 累加增量 -> 写回 BaseValue”三步样板代码。
- **安全边界**：拦截空 ASC、无效 `FGameplayAttribute` 及非有限浮点（`NaN` / `Inf` 输出 `LogGameAttribute` `Warning` 并拒绝写入），正常增量记录至 `LogGameAttribute`（`Verbose`）。

---

## 3. 依赖解耦设计

头文件中仅前置声明 `class UAbilitySystemComponent;`，避免传递引入重量级 `AbilitySystemComponent.h`，显著降低派生属性集编译开销。

---

## 4. 开发者派生属性集示例

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Attribute/UGameBaseAttributeSet.h"
#include "MyHeroAttributeSet.generated.h"

UCLASS()
class MYGAME_API UMyHeroAttributeSet : public UGameBaseAttributeSet {
    GENERATED_BODY()

public:
    ATTRIBUTE_ACCESSORS_BASIC(UMyHeroAttributeSet, Health)
    ATTRIBUTE_ACCESSORS_BASIC(UMyHeroAttributeSet, MaxHealth)
    ATTRIBUTE_ACCESSORS_BASIC(UMyHeroAttributeSet, IncomingDamage)

    void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override {
        Super::PreAttributeChange(Attribute, NewValue);
        if (Attribute == GetHealthAttribute()) {
            NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
        }
    }

    void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override {
        Super::PostGameplayEffectExecute(Data);
        if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute()) {
            const float DamageDone = MoveMetaAttribute(IncomingDamage);
            if (DamageDone > 0.0f) {
                ApplyAttributeToLocal(GetOwningAbilitySystemComponent(), GetHealthAttribute(), -DamageDone);
            }
        }
    }

private:
    UPROPERTY(BlueprintReadOnly, Category = "Attributes", meta = (AllowPrivateAccess = "true"))
    FGameplayAttributeData Health;

    UPROPERTY(BlueprintReadOnly, Category = "Attributes", meta = (AllowPrivateAccess = "true"))
    FGameplayAttributeData MaxHealth;

    UPROPERTY(BlueprintReadOnly, Category = "Attributes|Meta", meta = (AllowPrivateAccess = "true"))
    FGameplayAttributeData IncomingDamage;
};
```
