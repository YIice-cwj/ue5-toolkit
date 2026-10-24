# GameGameplay 伤害结算 ExecCalc API 参考

`UGameDamageExecution` 是一个动态通道化、无具体业务假设的泛型伤害结算计算基类。

头文件：`#include "AbilitySystem/UGameDamageExecution.h"`  
基类：`UGameplayEffectExecutionCalculation`

---

## 0. 运行时通道注册表

通道绑定由 `FGameDamageChannelRegistry`（`AbilitySystem/FGameDamageChannelRegistry.h`）统一托管：

- 按 ExecCalc 类隔离，线程安全（`FCriticalSection`）。
- 可在子类构造函数中声明，也可在运行期任意系统动态注册 / 注销。
- 查询时沿继承链合并，派生类同名通道覆盖基类，避免基类与派生类重复绑定导致数值双算。
- 类上不再有「CDO 构造后永久冻结」的写死契约，`Execute_Implementation` 也不再是 `final`，子类可按需扩展。

## 1. 结构体定义

### 1.1 `FDamageChannelBinding`
伤害通道到目标 GAS 元属性的输出映射规则：
```cpp
struct FDamageChannelBinding {
    /** 匹配的伤害通道标签（例如 Damage.Physical） */
    FGameplayTag ChannelTag;

    /** 目标写入的 GAS 元属性（例如 PhysicalDamageAttribute） */
    FGameplayAttribute TargetAttribute;

    /** 计算修饰操作（默认 Additive 累加） */
    EGameplayModOp::Type ModOp = EGameplayModOp::Additive;

    FDamageChannelBinding() = default;
    FDamageChannelBinding(const FGameplayTag& InTag, const FGameplayAttribute& InAttr, EGameplayModOp::Type InModOp = EGameplayModOp::Additive);
};
```

### 1.2 `FDamageModifier`
通道伤害修饰器（支持倍率缩放、固定加减减免与绝对覆写）：
```cpp
struct FDamageModifier {
    /** 百分比乘法倍率（默认 1.0f，用于元素克制或百分比增减伤） */
    float Multiplier = 1.0f;

    /** 固定加减数值（默认 0.0f，用于固定破甲或固定格挡抵扣） */
    float Additive = 0.0f;

    /** 是否强制覆写数值（若为 true，忽略上述计算直接取 OverrideValue） */
    bool bOverride = false;
    float OverrideValue = 0.0f;

    FDamageModifier() = default;
    explicit FDamageModifier(float InMult, float InAdd = 0.0f);
    static FDamageModifier MakeOverride(float InVal);
};
```

---

## 2. `UGameDamageExecution` 类接口

```cpp
UCLASS(Abstract)
class GAMEGAMEPLAY_API UGameDamageExecution : public UGameplayEffectExecutionCalculation {
    GENERATED_BODY()

public:
    virtual void Execute_Implementation(
        const FGameplayEffectCustomExecutionParameters& ExecutionParams,
        OUT FGameplayEffectCustomExecutionOutput& OutExecutionOutput
    ) const override;

    static bool RegisterRuntimeChannelBinding(
        UClass* InExecutionClass,
        const FGameplayTag& InChannelTag,
        const FGameplayAttribute& InTargetAttribute,
        EGameplayModOp::Type InModOp = EGameplayModOp::Additive
    );

    static bool UnregisterRuntimeChannelBinding(
        UClass* InExecutionClass,
        const FGameplayTag& InChannelTag
    );

protected:
    /**
     * @brief 注册具体捕获属性（受击方防御/抗性等，通常不快照传 false）
     */
    void RegisterTargetAttribute(
        const FGameplayAttribute& InAttribute,
        FGameplayEffectAttributeCaptureDefinition& OutDefinition,
        bool bSnapshot = false
    );

    /**
     * @brief 注册攻击端具体捕获属性（攻击力/穿透等，通常在施加前快照传 true）
     */
    void RegisterSourceAttribute(
        const FGameplayAttribute& InAttribute,
        FGameplayEffectAttributeCaptureDefinition& OutDefinition,
        bool bSnapshot = false
    );

    /**
     * @brief 注册通道映射规则（通常在子类构造函数中调用）
     */
    void RegisterChannelBinding(
        const FGameplayTag& ChannelTag,
        const FGameplayAttribute& TargetAttribute,
        EGameplayModOp::Type ModOp = EGameplayModOp::Additive
    );

    /**
     * @brief 判定指定通道是否应在本轮执行演算（默认检查 SetByCaller 是否存在）
     */
    virtual bool ShouldExecuteChannel(
        const FGameplayTag& ChannelTag,
        const FGameplayEffectCustomExecutionParameters& InExecutionParams
    ) const;

    /**
     * @brief 获取通道基础伤害（默认从 SetByCaller 读取）
     */
    virtual float GetChannelBaseDamage(
        const FGameplayTag& ChannelTag,
        const FGameplayEffectCustomExecutionParameters& InExecutionParams
    ) const;

    /**
     * @brief 自定义通道修饰管线虚函数（供子类扩展暴击、护甲减免、抗性计算）
     */
    virtual FDamageModifier ComputeChannelModifier(
        const FGameplayTag& ChannelTag,
        float BaseDamage,
        const FGameplayTagContainer& SourceTags,
        const FGameplayTagContainer& TargetTags,
        const FGameplayEffectCustomExecutionParameters& ExecutionParams
    ) const;

    /**
     * @brief 结算完成后的自定义反馈注入钩子（供子类根据战斗判定追加暴击、硬直等反馈标签或动态数值）
     */
    virtual void OnPostExecuteChannels(
        const FGameplayEffectCustomExecutionParameters& ExecutionParams,
        float TotalCalculatedDamage,
        struct FGameGameplayEffectContext* OutEffectContext
    ) const;
};
```

---

### 运行期注册示例

```cpp
void UMyCombatBootstrap::RegisterElementChannels()
{
    UGameDamageExecution::RegisterRuntimeChannelBinding(
        UMyDamageExecution::StaticClass(),
        FGameplayTag::RequestGameplayTag(TEXT("Damage.Fire")),
        UMyAttributeSet::GetFireDamageAttribute(),
        EGameplayModOp::Additive);

    UGameDamageExecution::UnregisterRuntimeChannelBinding(
        UMyDamageExecution::StaticClass(),
        FGameplayTag::RequestGameplayTag(TEXT("Damage.Fire")));
}
```

- 注册 / 注销线程安全，可在启动流程、模组加载或技能配置变更时调用。
- 未注册任何绑定的通道不会被结算；`ShouldExecuteChannel` 仍以 SetByCaller 是否存在为默认判据。

---

## 3. 上下文自动回写闭环

在 `Execute_Implementation` 演算全部通道并向 `OutExecutionOutput` 追加 Modifier 后，框架会自动提取当前 EffectSpec 中的 `FGameGameplayEffectContext`（由 `UGameAbilitySystemGlobals` 分配）：
1. 自动回写实际总伤害：`SetActualDamage(TotalCalculatedDamage)`；
2. 自动回写各通道实际数值：`SetActualChannelDamage(ChannelTag, FinalDamage)`（涵盖 100% 减免/免疫输出为 0.0f 的通道，确保下游表现层准确感知已判定通道）；
3. 触发虚函数钩子 `OnPostExecuteChannels`，允许子类根据攻击方或受击方的战斗标签容器，自由追加反馈标签（如 `Damage.Feedback.Critical`）与动态数值（如 `Damage.Feedback.HitStop`）；
4. 后续在受击端 `PostGameplayEffectExecute` 或表现层事件中可直接通过 Context 获取实际生效的伤害与反馈标签，并无缝导出为 `FGameDamageResult`。

