# GameGameplay 属性集通用基类 API 参考

`UGameBaseAttributeSet` 定位于**纯粹通用机制基类（零具体业务属性假设）**：它不硬编码任何具体属性（如生命、物理伤害、法术伤害），彻底放开由开发者在派生子类中自由声明与定义，同时为整个战斗框架提供标准的数值上下限钳制工具、阈值状态幂等闩、元属性归零搬运与增量落地接口。

头文件：`#include "AbilitySystem/UGameBaseAttributeSet.h"`  
基类：`UAttributeSet`

---

## 1. 类接口声明

```cpp
UCLASS(Abstract)
class GAMEGAMEPLAY_API UGameBaseAttributeSet : public UAttributeSet {
    GENERATED_BODY()

public:
    /**
     * @brief 属性上下限钳制（框架层唯一钳制入口）
     * @param InValue 待钳制数值
     * @param InMin 下限
     * @param InMax 上限
     * @return 钳制后的数值
     */
    [[nodiscard]] static float ClampAttribute(float InValue, float InMin, float InMax) noexcept;

    /**
     * @brief 阈值幂等闩：标签未授予则授予并返回 true（本轮首次达阈，调用方据此广播一次）
     * @param InAbilitySystemComponent 目标 ASC
     * @param InThresholdTag 临界状态标签
     * @return 是否为本轮首次达阈
     */
    [[nodiscard]] static bool BroadcastThresholdOnce(
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayTag& InThresholdTag
    );

    /**
     * @brief 提取元属性当前数值并立即清零归位（避免元属性在下一结算帧残留）
     * @param MetaAttributeData 待消费并清零的元属性数据
     * @return 提取出的当前数值
     */
    static float MoveMetaAttribute(FGameplayAttributeData& MetaAttributeData) noexcept;

    /**
     * @brief 将增量搬运落地至目标属性（框架层统一属性增量写入入口）
     * @param InAbilitySystemComponent 目标 ASC
     * @param InTargetAttribute 落地目标属性
     * @param InDelta 增量（伤害轨传负值表示扣减）
     */
    static void ApplyAttributeToLocal(
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayAttribute& InTargetAttribute,
        float InDelta
    );
};
```

---

## 2. 静态实用工具方法说明

### 2.1 `ClampAttribute`
```cpp
[[nodiscard]] static float ClampAttribute(float InValue, float InMin, float InMax) noexcept;
```
- **职责**：通用的上下限钳制，将数值限制在 `[InMin, InMax]` 之间。通常在派生属性集的 `PreAttributeChange` 与 `PreAttributeBaseChange` 中调用。

### 2.2 `BroadcastThresholdOnce`
```cpp
[[nodiscard]] static bool BroadcastThresholdOnce(UAbilitySystemComponent* InAbilitySystemComponent, const FGameplayTag& InThresholdTag);
```
- **职责**：**阈值状态幂等闩**。
- **机制**：若目标 ASC 尚未持有 `InThresholdTag`，则为其授予该松标签并返回 `true`（代表本轮首次突破临界阈值，供业务逻辑触发一次广播）；若已持有或 ASC 为空则返回 `false`。
- **典型场景**：残血（如生命首次低于 30%）触发狂暴或濒死警告，防止数值在临界线反复抖动时多次触发。恢复生命值时业务层可通过 `RemoveLooseGameplayTag` 复位该闩。

### 2.3 `MoveMetaAttribute`
```cpp
static float MoveMetaAttribute(FGameplayAttributeData& MetaAttributeData) noexcept;
```
- **职责**：提取当前元属性的值，并在提取后立即将其清零归位，避免元属性在下一个结算帧残留。常用于在 `PostGameplayEffectExecute` 中消费 ExecCalc 写入的伤害载体。

### 2.4 `ApplyAttributeToLocal`
```cpp
static void ApplyAttributeToLocal(UAbilitySystemComponent* InAbilitySystemComponent, const FGameplayAttribute& InTargetAttribute, float InDelta);
```
- **职责**：收敛“读当前值 -> 叠加增量 -> 写回 BaseValue”三段样板代码，统一将增量落地至目标属性。

---

## 3. 开发者派生属性集示例

开发者在具体游戏层（如 RPG、射击、格斗）自由派生 `UGameBaseAttributeSet`，按需声明任意基础属性与元属性：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/UGameBaseAttributeSet.h"
#include "MyHeroAttributeSet.generated.h"

UCLASS()
class MYGAME_API UMyHeroAttributeSet : public UGameBaseAttributeSet {
    GENERATED_BODY()

public:
    // 1. 使用 GAS 官方宏声明任意属性访问器
    ATTRIBUTE_ACCESSORS_BASIC(UMyHeroAttributeSet, Health)
    ATTRIBUTE_ACCESSORS_BASIC(UMyHeroAttributeSet, MaxHealth)
    ATTRIBUTE_ACCESSORS_BASIC(UMyHeroAttributeSet, Shield)

    // 2. 声明自定义元属性（作为 ExecCalc 的单次输出载体）
    ATTRIBUTE_ACCESSORS_BASIC(UMyHeroAttributeSet, IncomingDamage)

    virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override {
        Super::PostGameplayEffectExecute(Data);

        if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute()) {
            // 使用基类 MoveMetaAttribute 消费并归位元属性
            const float DamageDone = MoveMetaAttribute(IncomingDamage);
            if (DamageDone > 0.0f) {
                // 使用基类 ApplyAttributeToLocal 落地扣减生命
                ApplyAttributeToLocal(GetOwningAbilitySystemComponent(), GetHealthAttribute(), -DamageDone);
            }
        }
    }

private:
    UPROPERTY(BlueprintReadOnly, Category = "Attributes", meta = (AllowPrivateAccess = "true"))
    FGameplayAttributeData Health;

    UPROPERTY(BlueprintReadOnly, Category = "Attributes", meta = (AllowPrivateAccess = "true"))
    FGameplayAttributeData MaxHealth;

    UPROPERTY(BlueprintReadOnly, Category = "Attributes", meta = (AllowPrivateAccess = "true"))
    FGameplayAttributeData Shield;

    UPROPERTY(BlueprintReadOnly, Category = "Attributes|Meta", meta = (AllowPrivateAccess = "true"))
    FGameplayAttributeData IncomingDamage;
};
```
