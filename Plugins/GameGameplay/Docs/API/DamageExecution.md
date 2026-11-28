# GameGameplay 伤害结算 ExecCalc API 参考

> **一句话**：`UGameDamageExecution` 是游戏无关的泛型伤害结算基类，通道绑定由进程级 `FGameDamageChannelRegistry` 在运行期统一托管。
> **头文件**：`#include "Damage/UGameDamageExecution.h"`、`"Damage/FGameDamageChannelRegistry.h"`、`"Damage/FDamageChannelBinding.h"`、`"Damage/FDamageModifier.h"` · **线程**：`Execute_Implementation` 随 GAS 结算在 GameThread 执行，注册表内部 `FRWLock` 允许跨线程注册与查询 · **失败语义**：无错误码，空绑定早退、非法注册参数被忽略、未执行的通道不写输出

## 30 秒速览

- **结算**：`UGameDamageExecution`（`UCLASS(Abstract)`，父类 `UGameplayEffectExecutionCalculation`）按注册表登记的通道逐条演算，基类不包含任何具体属性或数值。
- **注册**：`FGameDamageChannelRegistry::Get()` 是运行期增删通道的唯一入口，按 ExecCalc 类隔离，遍历时沿继承链合并、派生类同名通道覆盖基类。
- **修饰**：`FDamageModifier` 统一表达倍率、固定加减与绝对覆写，`Evaluate` 覆写优先，否则为 `(Base + Additive) * Multiplier`。
- **回写**：通道被执行即写 `SetActualChannelDamage`；总伤害 `SetActualDamage` 与 `OnPostExecuteChannels` 仅在上下文为 `FGameGameplayEffectContext` 时触发。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `FGameDamageChannelRegistry::Get` | 获取进程级通道注册表单例 | 生命周期为进程级；`Reset()` 后需重新注册 |
| `FGameDamageChannelRegistry::Register` | 为指定 ExecCalc 注册或覆盖通道绑定 | 空类 / 非法绑定返回 `false`；内部 `FRWLock` 保护 |
| `FGameDamageChannelRegistry::Unregister` | 移除指定类的单个通道绑定 | 确实移除才返回 `true`，同时失效读缓存 |
| `FGameDamageChannelRegistry::ForEachBinding` | 遍历继承合并后的绑定快照 | 锁外执行闭包，回调内可安全重入注册 |
| `FDamageModifier::Evaluate` | 按覆写或线性公式演算最终伤害 | 纯值 `constexpr`/`noexcept`，无失败路径 |
| `UGameDamageExecution::Execute_Implementation` | 遍历注册表逐通道结算并回写 | 仅 GameThread；无任何绑定即直接返回 |
| `UGameDamageExecution::RegisterChannelBinding` | 将通道绑定登记到运行时注册表 | 参数无效时不注册，不参与结算 |
| `UGameDamageExecution::ComputeChannelModifier` | 计算单通道伤害修饰器 | 基类默认恒等修饰，倍率与减免不生效 |
| `UGameDamageExecution::ShouldExecuteChannel` | 判定通道本次是否参与结算 | 默认看 `SetByCallerTagMagnitudes` 是否含该标签 |

## 运行时注册表 `FGameDamageChannelRegistry`

```cpp
class GAMEGAMEPLAY_API FGameDamageChannelRegistry {
public:
    [[nodiscard]] static FGameDamageChannelRegistry& Get();

    bool Register(const UClass* InExecutionClass, const FDamageChannelBinding& InBinding);
    bool Unregister(const UClass* InExecutionClass, const FGameplayTag& InChannelTag);
    int32 UnregisterAll(const UClass* InExecutionClass);
    void ForEachBinding(
        const UClass* InExecutionClass,
        TFunctionRef<void(TConstArrayView<FDamageChannelBinding>)> InAction
    ) const;
    [[nodiscard]] int32 Num(const UClass* InExecutionClass) const;
    void Reset();
};
```

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `Get()` | — | `FGameDamageChannelRegistry&` | 进程级单例 |
| `Register` | 归属 ExecCalc 类、`FDamageChannelBinding` | `bool` | 新增或覆盖同类同标签绑定；参数非法返回 `false` |
| `Unregister` | 类、通道标签 | `bool` | 确实移除条目才返回 `true` 并失效读缓存 |
| `UnregisterAll` | 类 | `int32` | 返回被移除的绑定数量 |
| `ForEachBinding` | 类、只读闭包 | — | 传入继承合并后的绑定切片 |
| `Num` | 类 | `int32` | 仅统计该类直属绑定，不含继承 |
| `Reset` | — | — | 清空全部绑定与读缓存 |

- **线程**：内部 `FRWLock` 读写锁保护；`ForEachBinding` 在锁内把合并结果拷贝至 `TInlineAllocator<8>` 栈快照，释锁后执行闭包，回调内可安全重入注册。
- **生命周期**：进程级单例；`Reset()` 供测试与热重载清空全部状态，之后需重新注册。
- **失败语义**：空类或无效绑定拒绝注册并告警；`Unregister` 失败返回 `false`，`UnregisterAll` / `Num` 对空类返回 `0`；不抛异常。
- **幂等与缓存**：完全相同的绑定重复 `Register` 直接返回 `true` 且不失效读缓存；内容变化或成功注销时才重置读缓存。

### 最小示例

```cpp
#include "Damage/FGameDamageChannelRegistry.h"

void UMyCombatBootstrap::RegisterElementChannels() {
    auto& Registry = FGameDamageChannelRegistry::Get();
    Registry.Register(
        UMyDamageExecution::StaticClass(),
        FDamageChannelBinding(
            FGameplayTag::RequestGameplayTag(TEXT("Damage.Fire")),
            UMyAttributeSet::GetFireDamageAttribute(),
            EGameplayModOp::Additive
        )
    );

    Registry.Unregister(
        UMyDamageExecution::StaticClass(),
        FGameplayTag::RequestGameplayTag(TEXT("Damage.Fire"))
    );
}
```

## 绑定与修饰结构体

### `FDamageChannelBinding`

```cpp
struct FDamageChannelBinding {
    FGameplayTag ChannelTag;
    FGameplayAttribute TargetAttribute;
    EGameplayModOp::Type ModOp = EGameplayModOp::Additive;

    FDamageChannelBinding() = default;
    FDamageChannelBinding(
        const FGameplayTag& InTag,
        const FGameplayAttribute& InAttr,
        EGameplayModOp::Type InModOp = EGameplayModOp::Additive
    );

    [[nodiscard]] FORCEINLINE bool IsValid() const noexcept;
    [[nodiscard]] bool operator==(const FDamageChannelBinding& Other) const noexcept;
};
```

| 成员 | 类型 | 说明 |
| :--- | :--- | :--- |
| `ChannelTag` | `FGameplayTag` | 匹配的伤害通道标签 |
| `TargetAttribute` | `FGameplayAttribute` | 写入的目标元属性 |
| `ModOp` | `EGameplayModOp::Type` | 修饰操作，默认 `Additive` |
| `IsValid()` | `bool` | 通道标签与目标属性均有效时为 `true` |
| `operator==` | `bool` | 标签、属性与修饰操作全部一致时为 `true` |

- **线程/生命周期**：纯值类型，随注册表拷贝存储，无堆分配与共享状态。
- **失败语义**：`IsValid()` 为 `false` 的绑定会被注册表拒绝，不参与结算。

### `FDamageModifier`

```cpp
struct FDamageModifier {
    float Multiplier = 1.0f;
    float Additive = 0.0f;
    bool bOverride = false;
    float OverrideValue = 0.0f;

    constexpr FDamageModifier() noexcept = default;
    constexpr explicit FDamageModifier(float InMult, float InAdd = 0.0f) noexcept;
    [[nodiscard]] static constexpr FDamageModifier MakeOverride(float InVal) noexcept;

    [[nodiscard]] FORCEINLINE constexpr float Evaluate(float BaseDamage) const noexcept;
    [[nodiscard]] bool operator==(const FDamageModifier& Other) const noexcept;
};
```

| 成员 | 说明 |
| :--- | :--- |
| `Multiplier` | 百分比乘法倍率，默认 `1.0f` |
| `Additive` | 固定加减值，默认 `0.0f` |
| `bOverride` / `OverrideValue` | 绝对覆写开关与覆写值，仅 `bOverride == true` 时生效 |
| `MakeOverride(Value)` | 构造绝对覆写修饰器 |
| `Evaluate(Base)` | `bOverride ? OverrideValue : (Base + Additive) * Multiplier` |
| `operator==` | 覆写模式仅比较覆写值，否则比较倍率与固定加减 |

- **线程/生命周期**：纯值类型，`constexpr` / `noexcept`，无堆分配，可跨线程传值。
- **失败语义**：无失败路径；未设置覆写时按线性公式演算。

### 最小示例

```cpp
FDamageModifier ArmorMitigation(1.25f, -5.0f);
const float AfterArmor = ArmorMitigation.Evaluate(BaseDamage);

const FDamageModifier TrueDamage = FDamageModifier::MakeOverride(BaseDamage);
const float IgnoredArmor = TrueDamage.Evaluate(BaseDamage);
```

## `UGameDamageExecution`

```cpp
UCLASS(Abstract)
class GAMEGAMEPLAY_API UGameDamageExecution : public UGameplayEffectExecutionCalculation {
    GENERATED_BODY()

public:
    void Execute_Implementation(
        const FGameplayEffectCustomExecutionParameters& ExecutionParams,
        FGameplayEffectCustomExecutionOutput& OutExecutionOutput
    ) const override;

protected:
    void RegisterTargetAttribute(
        const FGameplayAttribute& InAttribute,
        FGameplayEffectAttributeCaptureDefinition& OutDefinition,
        bool bSnapshot = false
    );

    void RegisterSourceAttribute(
        const FGameplayAttribute& InAttribute,
        FGameplayEffectAttributeCaptureDefinition& OutDefinition,
        bool bSnapshot = true
    );

    void RegisterChannelBinding(
        const FGameplayTag& ChannelTag,
        const FGameplayAttribute& TargetAttribute,
        EGameplayModOp::Type ModOp = EGameplayModOp::Additive
    );

    virtual bool ShouldExecuteChannel(
        const FGameplayTag& ChannelTag,
        const FGameplayEffectCustomExecutionParameters& InExecutionParams
    ) const;

    virtual float GetChannelBaseDamage(
        const FGameplayTag& ChannelTag,
        const FGameplayEffectCustomExecutionParameters& InExecutionParams
    ) const;

    virtual FDamageModifier ComputeChannelModifier(
        const FGameplayTag& ChannelTag,
        float BaseDamage,
        const FGameplayTagContainer& SourceTags,
        const FGameplayTagContainer& TargetTags,
        const FGameplayEffectCustomExecutionParameters& ExecutionParams
    ) const;

    virtual void OnPostExecuteChannels(
        const FGameplayEffectCustomExecutionParameters& ExecutionParams,
        float TotalCalculatedDamage,
        FGameGameplayEffectContext* OutEffectContext
    ) const;
};
```

| 成员 | 参数 | 返回 | 默认行为 |
| :--- | :--- | :--- | :--- |
| `Execute_Implementation` | 结算参数、执行输出 | `void` | 遍历注册表绑定并逐通道结算 |
| `RegisterTargetAttribute` | 属性、捕获定义、`bSnapshot = false` | `void` | 以 `Target` 源注册到 `RelevantAttributesToCapture` |
| `RegisterSourceAttribute` | 属性、捕获定义、`bSnapshot = true` | `void` | 以 `Source` 源注册到 `RelevantAttributesToCapture` |
| `RegisterChannelBinding` | 通道标签、目标属性、修饰操作 | `void` | 参数有效时委托注册表注册 |
| `ShouldExecuteChannel` | 通道标签、结算参数 | `bool` | Spec 的 `SetByCallerTagMagnitudes` 含该标签时为 `true` |
| `GetChannelBaseDamage` | 通道标签、结算参数 | `float` | 取该标签的 SetByCaller 数值，缺省 `0.0f` |
| `ComputeChannelModifier` | 通道、基础伤害、双方标签、结算参数 | `FDamageModifier` | 返回恒等修饰器（不改变基础伤害） |
| `OnPostExecuteChannels` | 结算参数、总伤害、效果上下文 | `void` | 空实现钩子 |

- **线程**：`Execute_Implementation` 为 `const`，在 GameThread 的 GE 结算栈上运行；遍历期间只读通道快照。
- **生命周期**：绑定跟随 ExecCalc 类存放于注册表，结算不修改注册表；实例销毁不影响绑定。
- **失败语义**：该类无任何绑定时立即返回，不回写上下文、不触发 `OnPostExecuteChannels`；未覆写 `ComputeChannelModifier` 时按基础伤害直出。

### 最小示例

```cpp
UMyDamageExecution::UMyDamageExecution() {
    RegisterSourceAttribute(UMyAttributeSet::GetAttackPowerAttribute(), AttackPowerCapture, true);
    RegisterTargetAttribute(UMyAttributeSet::GetArmorAttribute(), ArmorCapture, false);
    RegisterChannelBinding(
        FGameplayTag::RequestGameplayTag(TEXT("Damage.Physical")),
        UMyAttributeSet::GetPhysicalDamageAttribute(),
        EGameplayModOp::Additive
    );
}
```

## 结算契约

- **预测事件 ID**：仅当 `ExecutionParams.GetPredictionKey().IsValidKey()` 为真时写入 `FeedbackEventId`；无预测执行不覆写外部预置值。
- **聚合标签零拷贝**：优先直接引用 `CapturedSourceTags` / `CapturedTargetTags` 的 `GetAggregatedTags()`（涵盖 Actor、GE AssetTags 与 Spec DynamicAssetTags）；手工构造且未捕获的 Spec 回退合并 ASC 标签与 Spec 标签。
- **逐通道结算**：
  1. `ShouldExecuteChannel` 为假则跳过；
  2. `FinalDamage = ComputeChannelModifier(...).Evaluate(BaseDamage)`；
  3. 通道被执行即回写 `SetActualChannelDamage(ChannelTag, FinalDamage)`，包含 `0.0f` 的免疫/减免结果；
  4. 当 `ModOp == EGameplayModOp::Override` 或 `FinalDamage` 非零时，向 `OutExecutionOutput` 输出 Modifier 并累加总伤害，确保 `Override(0.0f)` 生效且支持治疗负值。
- **结算结束**：上下文为 `FGameGameplayEffectContext` 时写入 `SetActualDamage` 并回调 `OnPostExecuteChannels`。

## 常见错误与边界

- **通道未注册**：ExecCalc 只遍历注册表中的绑定，未注册的通道不会读取 SetByCaller 数值，也不会产生输出。
- **空绑定早退**：类无任何绑定或全部被注销时，`Execute_Implementation` 直接返回，不写任何上下文。
- **零值通道**：`FinalDamage == 0` 仍会回写通道实际伤害（便于表现层识别免疫），但不输出 Modifier、不计入总伤害；`Override` 不受此限制。
- **预测键缺失**：无有效预测键时不会生成新的 `FeedbackEventId`，跨端去重依赖外部预置值。
- **基类默认修饰**：不覆写 `ComputeChannelModifier` 即为恒等修饰，倍率与减免不会生效。
- **标签回退**：未捕获标签的手工 Spec 会额外合并 ASC 标签，热路径请使用正常的捕获流程以避免临时容器分配。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [伤害结算管线](../Architecture/DamageExecutionPipeline.md)
- [伤害结算指南](../Guides/DamageExecutionGuide.md)
