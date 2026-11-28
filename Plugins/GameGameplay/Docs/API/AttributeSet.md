# GameGameplay 属性集通用基类 API 参考

> **一句话**：不声明任何业务属性的通用属性集基类，提供阈值幂等闩、元属性归零与安全本地增量工具。
> **头文件**：`Attribute/UGameBaseAttributeSet.h`
> **线程**：仅限 GameThread；属性与 GameplayTag 操作须在 ASC 所在线程执行。
> **失败语义**：空 ASC、无效标签、无效属性、`NaN` / `Inf` 增量均安全短路；非有限增量输出 `Warning` 且拒绝写入。

## 30 秒速览

- **零业务属性**：`UGameBaseAttributeSet` 是 `UCLASS(Abstract)`，不硬编码任何属性，也不包装数值钳制；上下限钳制由派生属性集直接使用 `FMath::Clamp`。
- **阈值幂等闩**：`BroadcastThresholdOnce` 用松标签表达“本轮首次达阈”，`ResetThreshold` 移除标签以允许再次触发。
- **元属性归零**：`MoveMetaAttribute` 读取当前值后同时清零 `BaseValue` 与 `CurrentValue`，避免跨结算帧残留。
- **安全增量**：`ApplyAttributeToLocal` 收敛“读旧基础值 → 加增量 → 写回基础值”，并拦截非有限浮点。
- **复制与元属性契约**：基类不声明具体属性、不实现复制；派生属性集如需网络复制，按 GAS 原生方式声明，并通过 `ASC->AddAttributeSetSubobject` 注册。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `UGameBaseAttributeSet::BroadcastThresholdOnce` | 首次达阈授予松标签并返回 `true` | 空 ASC、无效标签或已持有返回 `false`；不广播委托 |
| `UGameBaseAttributeSet::ResetThreshold` | 移除阈值标签以允许再次触发 | 未持有有效标签时为空操作 |
| `UGameBaseAttributeSet::MoveMetaAttribute` | 取元属性当前值并同时清零 Base 与 Current | 头文件强制内联；不依赖 ASC |
| `UGameBaseAttributeSet::ApplyAttributeToLocal` | 把增量写入目标属性基础值 | `NaN` / `Inf` 输出 `Warning` 并拒绝写入；不做上下限钳制 |
| `UGameBaseAttributeSet::PreAttributeChange`（派生覆写） | 在属性变更前钳制数值 | 须先调用 `Super`；上下限由派生属性集自行 `FMath::Clamp` |
| `UGameBaseAttributeSet::PostGameplayEffectExecute`（派生覆写） | 结算后消费元属性并落地伤害 | 须先调用 `Super`；未消费的元属性会残留到下一结算帧 |

## `UGameBaseAttributeSet`

### 接口

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

    [[nodiscard]] FORCEINLINE static float MoveMetaAttribute(FGameplayAttributeData& MetaAttributeData) noexcept;

    static void ApplyAttributeToLocal(
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayAttribute& InTargetAttribute,
        float InDelta
    );
};
```

### 参数与返回

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `BroadcastThresholdOnce` | `InAbilitySystemComponent`、`InThresholdTag` | `bool` | 首次达阈授予松标签并返回 `true`；已持有、空 ASC 或无效标签返回 `false` |
| `ResetThreshold` | `InAbilitySystemComponent`、`InThresholdTag` | `void` | ASC 持有有效标签时移除；否则空操作 |
| `MoveMetaAttribute` | `MetaAttributeData` | `float` | 返回提取的当前值，并把 Base 与 Current 同时归零 |
| `ApplyAttributeToLocal` | `InAbilitySystemComponent`、`InTargetAttribute`、`InDelta` | `void` | 把有限增量写入目标属性基础值 |

### 线程 / 生命周期 / 失败语义

- **阈值状态由 GameplayTag 驱动**：`BroadcastThresholdOnce` 通过 `HasMatchingGameplayTag` 判定标签（含父标签匹配），未持有时调用 `AddLooseGameplayTag`；本方法不广播委托，只返回是否为本轮首次达阈。
- **无效输入短路**：空 ASC 或 `!InThresholdTag.IsValid()` 时，`BroadcastThresholdOnce` 返回 `false`，`ResetThreshold` 直接返回，不污染 ASC 标签集。
- **元属性语义**：`MoveMetaAttribute` 定义在头文件并强制内联，不依赖 ASC；先取 `GetCurrentValue()`，再把 `SetBaseValue(0.0f)` 与 `SetCurrentValue(0.0f)` 都归零。
- **增量边界**：`ApplyAttributeToLocal` 对空 ASC、无效 `FGameplayAttribute` 直接返回；`!FMath::IsFinite(InDelta)` 时输出 `LogGameAttribute` `Warning` 并拒绝写入。
- **不做钳制**：`ApplyAttributeToLocal` 只做基础值加减，不负责上下限；派生属性集应在 `PreAttributeChange` / `PostGameplayEffectExecute` 中用 `FMath::Clamp` 处理。
- **复制契约**：本基类不声明任何 `UPROPERTY`，也不重写 `GetLifetimeReplicatedProps`；派生属性集如需网络复制，应自行按 GAS 规范声明 `FGameplayAttributeData` 的复制与 `OnRep`，再通过 `ASC->AddAttributeSetSubobject` 注册。
- **头文件依赖**：公开头仅前置声明 `class UAbilitySystemComponent;`，避免传递引入 `AbilitySystemComponent.h`。
- **日志**：阈值触发、重置与增量落地成功均写入 `LogGameAttribute` 的 `Verbose` 级别。

### 最小示例

```cpp
UCLASS()
class MYGAME_API UMyHeroAttributeSet : public UGameBaseAttributeSet {
    GENERATED_BODY()

public:
    ATTRIBUTE_ACCESSORS(UMyHeroAttributeSet, Health)
    ATTRIBUTE_ACCESSORS(UMyHeroAttributeSet, MaxHealth)
    ATTRIBUTE_ACCESSORS(UMyHeroAttributeSet, IncomingDamage)

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

## 常见错误与边界

- **只触发不复位**：`BroadcastThresholdOnce` 在标签已存在时永远返回 `false`；恢复后必须调用 `ResetThreshold` 才能再次触发。
- **每帧期待首次达阈**：该方法只表达“本轮首次”，重复调用不会重复返回 `true`。
- **忘记消费元属性**：未调用 `MoveMetaAttribute` 时元属性会保留到下一结算帧，可能被重复读取。
- **期待自动钳制**：`ApplyAttributeToLocal` 不做上下限处理；生命值等边界应在派生属性集的 `PreAttributeChange` 或结算回调中钳制。
- **无效标签/ASC 静默忽略**：阈值与增量方法不会报错，也不会修改 ASC；调用方需自行保证参数有效。
- **遗漏注册或复制声明**：属性集未通过 `ASC->AddAttributeSetSubobject` 注册，或需要复制却未按 GAS 规范声明时，客户端可能读不到期望数值。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [快速上手指南](../Guides/QuickStart.md)
- [自定义伤害通道与 ExecCalc 指南](../Guides/DamageExecutionGuide.md)
