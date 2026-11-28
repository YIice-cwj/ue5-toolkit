# GameGameplay 属性表现映射 API 参考

> **一句话**：把 ASC 属性变更收敛为蓝图与原生双轨委托、并支持对象池复用的属性映射工具 API。
> **头文件**：`Attribute/UGameplayAttributeValueMapping.h`
> **线程**：仅限 GameThread；属性变更回调由 ASC 的数值变更委托在 GameThread 触发。
> **失败语义**：工厂在 ASC 为空或属性无效时返回 `nullptr`；无效 `Bind` 忽略并保留原绑定；未绑定或 ASC 失效时查询静默跳过。

## 30 秒速览

- **双轨广播**：`OnValueChanged` 供蓝图订阅，`OnValueChangedNative` 供 C++ 直接订阅，不经过蓝图动态委托路径。
- **工厂默认不广播**：`CreateAttributeValueMapping` 内部以 `Bind(..., false)` 绑定；调用方应先订阅，再调用 `QueryAndBroadcast()` 拉取初值。
- **安全重绑**：`Bind` 参数有效时先解绑旧源，再按需立即广播新源当前值；参数无效时保留原绑定。
- **GC 安全**：`IsBound()` 同时校验委托句柄与 ASC 弱引用；ASC 失效后查询静默跳过，不向 UI 误播 `0.0f`。
- **池化复位**：入池解绑并清空全部订阅者；出池后可直接重新 `Bind` 复用。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `UGameplayAttributeValueMapping::CreateAttributeValueMapping`（蓝图） | 以 `Outer` 为宿主创建映射并完成绑定 | ASC 为空或属性无效返回 `nullptr`；工厂不广播初值 |
| `UGameplayAttributeValueMapping::CreateAttributeValueMapping`（C++） | 双参重载，以 ASC 为宿主创建 | 返回对象需 `UPROPERTY` / 强引用持有，避免被 GC 回收 |
| `UGameplayAttributeValueMapping::Bind` | 重绑 ASC 与属性 | 参数无效时保留原绑定；`bBroadcastImmediately` 默认 `true` |
| `UGameplayAttributeValueMapping::Unbind` | 解绑当前源 | 幂等；对象销毁或入池时自动调用 |
| `UGameplayAttributeValueMapping::IsBound` | 判断绑定是否仍有效 | 委托句柄与 ASC 弱引用均有效才返回 `true` |
| `UGameplayAttributeValueMapping::GetCurrentValue` | 读取当前属性数值 | 未绑定或 ASC 失效返回 `0.0f` |
| `UGameplayAttributeValueMapping::QueryAndBroadcast` | 主动拉取当前值并广播一次 | 未绑定或 ASC 失效静默跳过；仅投递给已订阅者 |
| `UGameplayAttributeValueMapping::OnValueChanged` / `OnValueChangedNative` | 蓝图与 C++ 双轨变更委托 | 池化入池清空全部订阅者，出池需重新订阅 |

## 委托

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMappedAttributeChanged, float, NewValue, float, OldValue);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMappedAttributeChangedNative, float, float);
```

| 委托 | 订阅方 | 参数 |
| :--- | :--- | :--- |
| `FOnMappedAttributeChanged` | 蓝图 / UMG | `NewValue`、`OldValue` |
| `FOnMappedAttributeChangedNative` | C++ Lambda / 成员函数 | `NewValue`、`OldValue` |

## `UGameplayAttributeValueMapping`

### 接口

```cpp
UCLASS(BlueprintType)
class GAMEGAMEPLAY_API UGameplayAttributeValueMapping : public UObject, public IGamePoolable {
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable, Category = "Game|GAS")
    FOnMappedAttributeChanged OnValueChanged;

    FOnMappedAttributeChangedNative OnValueChangedNative;

    UFUNCTION(BlueprintCallable, Category = "Game|GAS", meta = (DefaultToSelf = "Outer"))
    static UGameplayAttributeValueMapping* CreateAttributeValueMapping(
        UObject* Outer,
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayAttribute& InAttribute
    );

    static UGameplayAttributeValueMapping* CreateAttributeValueMapping(
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayAttribute& InAttribute
    );

    void BeginDestroy() override;
    void OnAcquireFromPool_Implementation() override;
    void OnReleaseToPool_Implementation() override;

    UFUNCTION(BlueprintCallable, Category = "Game|GAS")
    void Bind(
        UAbilitySystemComponent* InAbilitySystemComponent,
        const FGameplayAttribute& InAttribute,
        bool bBroadcastImmediately = true
    );

    UFUNCTION(BlueprintCallable, Category = "Game|GAS")
    void Unbind();

    UFUNCTION(BlueprintPure, Category = "Game|GAS")
    [[nodiscard]] const FGameplayAttribute& GetBoundAttribute() const noexcept;

    UFUNCTION(BlueprintPure, Category = "Game|GAS")
    [[nodiscard]] UAbilitySystemComponent* GetBoundAbilitySystemComponent() const noexcept;

    UFUNCTION(BlueprintPure, Category = "Game|GAS")
    [[nodiscard]] bool IsBound() const noexcept;

    UFUNCTION(BlueprintPure, Category = "Game|GAS")
    [[nodiscard]] float GetCurrentValue() const;

    UFUNCTION(BlueprintCallable, Category = "Game|GAS")
    void QueryAndBroadcast();
};
```

### 参数与返回

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `CreateAttributeValueMapping`（蓝图） | `Outer`、`InAbilitySystemComponent`、`InAttribute` | `UGameplayAttributeValueMapping*` | `ASC` 为空或属性无效返回 `nullptr`；`Outer` 为空时以 ASC 为宿主 |
| `CreateAttributeValueMapping`（C++） | `InAbilitySystemComponent`、`InAttribute` | `UGameplayAttributeValueMapping*` | 双参便利重载，内部以 ASC 为 `Outer` |
| `Bind` | `InAbilitySystemComponent`、`InAttribute`、`bBroadcastImmediately = true` | `void` | 有效参数先解绑旧源；无效参数直接返回 |
| `Unbind` | 无 | `void` | 幂等解绑；对象销毁或入池时自动调用 |
| `GetBoundAttribute` | 无 | `const FGameplayAttribute&` | 当前绑定属性；未绑定时无效 |
| `GetBoundAbilitySystemComponent` | 无 | `UAbilitySystemComponent*` | 当前绑定 ASC；未绑定或失效返回 `nullptr` |
| `IsBound` | 无 | `bool` | 委托句柄与 ASC 弱引用均有效才返回 `true` |
| `GetCurrentValue` | 无 | `float` | 读取 `GetNumericAttribute`；未绑定或 ASC 失效返回 `0.0f` |
| `QueryAndBroadcast` | 无 | `void` | 主动拉取当前值并广播一次；未绑定或 ASC 失效时静默跳过 |

### 线程 / 生命周期 / 失败语义

- **工厂**：先校验 `InAbilitySystemComponent != nullptr` 且 `InAttribute.IsValid()`，再以 `Outer != nullptr ? Outer : ASC` 为宿主创建对象，并调用 `Bind(ASC, Attribute, false)`。因此工厂返回时已完成绑定，但**不会立即广播初值**。
- **绑定**：`Bind` 参数有效时先 `Unbind()`，记录 ASC 弱引用与属性，再通过 `AddUObject` 注册属性变更委托；`bBroadcastImmediately == true` 时追加一次 `QueryAndBroadcast()`。
- **无效重绑**：`Bind(nullptr, Attribute)` 或 `Bind(ASC, 无效属性)` 在解绑前直接返回，原有绑定与订阅保持不变。
- **解绑**：仅当委托句柄有效、ASC 仍有效且未处于 `RF_BeginDestroyed` / `RF_FinishDestroyed` 时移除委托；随后重置句柄、属性与 ASC 弱引用。
- **数值查询**：`GetCurrentValue` 直接调用 `GetNumericAttribute`，未绑定或 ASC 已回收时返回 `0.0f`；`QueryAndBroadcast` 先检查 `IsBound()`，因此不会把该 `0.0f` 误播给 UI。
- **广播内容**：`QueryAndBroadcast` 以当前值同时作为 `NewValue` 与 `OldValue` 广播到蓝图与原生委托；属性实际变更时转发 ASC 提供的 `NewValue` / `OldValue`。
- **对象生命周期**：`BeginDestroy` 调用 `Unbind`；`OnAcquireFromPool` 调用 `Unbind` 保持洁净待绑定态；`OnReleaseToPool` 解绑并清空 `OnValueChanged`、`OnValueChangedNative` 的全部订阅者。

### 最小示例

```cpp
UGameplayAttributeValueMapping* Mapping = UGameplayAttributeValueMapping::CreateAttributeValueMapping(
    this,
    HeroASC,
    UMyHeroAttributeSet::GetHealthAttribute()
);

Mapping->OnValueChangedNative.AddLambda([this](float NewValue, float OldValue) {
    UpdateHealthBar(NewValue, OldValue);
});

Mapping->QueryAndBroadcast();
```

## 常见错误与边界

- **期待工厂广播初值**：工厂固定使用 `bBroadcastImmediately = false`；必须先订阅再显式调用 `QueryAndBroadcast()`。
- **先查询后订阅**：`QueryAndBroadcast()` 只广播给当下已注册的订阅者，提前调用会丢失本次初值。
- **用 `Bind(nullptr, ...)` 代替解绑**：无效参数会保留原绑定；需要解绑时调用 `Unbind()`。
- **映射对象未持有引用**：`UGameplayAttributeValueMapping` 是 `UObject`，蓝图或 C++ 宿主应使用 `UPROPERTY` / 强引用持有，避免被 GC 回收。
- **ASC 销毁后继续查询**：`IsBound()` 转为 `false`，`GetCurrentValue()` 返回 `0.0f`，`QueryAndBroadcast()` 静默跳过；需要重新 `Bind` 到新 ASC。
- **池化后沿用旧订阅**：`OnReleaseToPool` 会清空两类订阅者，出池复用后必须重新订阅并绑定。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [UMG 界面绑定指南](../Guides/UIBindingGuide.md)
- [快速上手指南](../Guides/QuickStart.md)
