# GameGameplay 伤害结算 ExecCalc API 参考

`UGameDamageExecution` 是一个动态通道化、无具体业务假设的泛型伤害结算计算基类，通道绑定由 `FGameDamageChannelRegistry` 统一托管。

头文件：
- `#include "Damage/UGameDamageExecution.h"`
- `#include "Damage/FGameDamageChannelRegistry.h"`

基类：`UGameplayEffectExecutionCalculation`

---

## 0. 运行时通道注册表 (`FGameDamageChannelRegistry`)

通道绑定由 `FGameDamageChannelRegistry`（`Damage/FGameDamageChannelRegistry.h`）统一托管，是运行期注册/注销的**唯一权威入口**：

- **按 ExecCalc 类隔离，线程安全**：内部采用 `FRWLock` 读写锁保护；`Register` 返回 `bool`（同绑定幂等短路不误清缓存），`Unregister` / `UnregisterAll` 仅在确实移除条目后失效读缓存。
- **锁外栈内联快照遍历 (`ForEachBinding`)**：在极短的读锁（冷启动构建时写锁）内将继承链扁平化绑定拷贝至栈上 `TInlineAllocator<8>` 快照后**立即释锁**，在无锁状态下调用业务闭包 `InAction`。既在常见通道数（≤8）下保持零堆分配，又彻底根除回调内部重入注册或交叉锁引发的死锁。
- **继承合并**：沿类继承链向上合并，派生类同名通道覆盖基类，杜绝数值双算。

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

---

## 1. 结构体定义

### 1.1 `FDamageChannelBinding`
```cpp
struct FDamageChannelBinding {
    FGameplayTag ChannelTag;
    FGameplayAttribute TargetAttribute;
    EGameplayModOp::Type ModOp = EGameplayModOp::Additive;

    FDamageChannelBinding() = default;
    FDamageChannelBinding(const FGameplayTag& InTag, const FGameplayAttribute& InAttr, EGameplayModOp::Type InModOp = EGameplayModOp::Additive);

    [[nodiscard]] FORCEINLINE bool IsValid() const noexcept;
    [[nodiscard]] bool operator==(const FDamageChannelBinding& Other) const noexcept;
};
```

### 1.2 `FDamageModifier`
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

---

## 2. `UGameDamageExecution` 类接口

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

### 运行期动态注册示例

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

---

## 3. 上下文自动回写与结算契约

- **通道绑定为空** → 立即返回，不回写上下文且不触发 `OnPostExecuteChannels`。
- **预测事件 ID 保护**：仅当 `ExecutionParams.GetPredictionKey().IsValidKey()` 为真时才更新 `FeedbackEventId`，无预测执行绝不覆写外部预置的事件 ID。
- **聚合标签零拷贝**：优先直接引用 `CapturedSourceTags` / `CapturedTargetTags` 的 `GetAggregatedTags()`（完整涵盖 Actor、GE AssetTags 与 Spec DynamicAssetTags）；若为手工构造未捕获的 Spec，则自动回退合并 ASC 标签与 Spec 标签。
- **逐通道结算**：
  1. `ShouldExecuteChannel` 为假则跳过；
  2. `FinalDamage = ComputeChannelModifier(...).Evaluate(BaseDamage)`；
  3. 只要通道被执行即回写 `SetActualChannelDamage(ChannelTag, FinalDamage)`（包含 0.0f 免疫/减免结果）；
  4. 当 `Binding.ModOp == EGameplayModOp::Override` 或 `!FMath::IsNearlyZero(FinalDamage)` 时向 `OutExecutionOutput` 输出 Modifier 并累加至 `TotalCalculatedDamage`（确保 `Override(0.0f)` 生效且支持治疗负值）。
- **结算结束**：若存在 `FGameGameplayEffectContext`，回写 `SetActualDamage(TotalCalculatedDamage)` 并回调 `OnPostExecuteChannels`。
