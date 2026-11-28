# GameGameplayCore 生命与生死状态组件 API 参考

> **一句话**：主生命标签绑定、存活判定、致死与复活状态机的专职组件，与 `UGameStatComponent` 协同工作。
> **头文件**：`Stat/UGameVitalComponent.h` · **线程**：GameThread；组件 Tick 关闭，纯事件驱动
> **失败语义**：接口均为 `void`；非权威、非有限比例、属性未注册或 `Max <= Min` 时记录 `Warning` 并空操作

## 30 秒速览

- **职责单一**：只管理主生命标签、`bVitalAlive` 存活态与复活；不越权改写 `UGameStatComponent` 的基础值。
- **绑定**：`BindStatComponent` 显式绑定；`BeginPlay` 自动发现宿主数值组件并补齐订阅，支持蓝图 CDO 预配置主生命标签。
- **判定**：未设置主生命标签时恒为存活；设置后派生当前值 `> Min` 为存活，否则阵亡。
- **复活**：`ReviveVital(FillPercent)` 以 `Min + (Max - Min) * Clamp(FillPercent, 0, 1)` 恢复数值。
- **复制**：`bVitalAlive` 网络复制，`OnRep_VitalAlive` 按上次广播值去重；入池静默重置。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `UGameVitalComponent::BindStatComponent` | 绑定目标数值组件 | `nullptr` 为显式解绑，之后不再自动回绑 |
| `UGameVitalComponent::SetPrimaryVitalTag` | 设置主生命标签并重新评估 | 标签未变直接返回；空标签恢复存活态 |
| `UGameVitalComponent::GetPrimaryVitalTag` | 查询当前主生命标签 | 未设置返回无效 Tag |
| `UGameVitalComponent::IsVitalAlive` | 查询当前存活态 | 未设置主生命标签时恒为 `true` |
| `UGameVitalComponent::ReviveVital` | 按 `FillPercent` 回填主生命并复活 | 非权威 / `Max <= Min` / 属性未注册时记录 `Warning` 空操作 |
| `UGameVitalComponent::EvaluateVitalState` | 依据派生值手动重评估存活态 | 仅 GameThread；仅在状态跃迁时广播 |

## 委托

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnGameVitalStateChanged,
    bool, bIsAlive,
    const FGameplayTag&, VitalTag
);

DECLARE_MULTICAST_DELEGATE_TwoParams(
    FOnGameVitalStateChangedNative,
    bool,
    const FGameplayTag&
);
```

| 委托 | 参数 | 触发时机 |
| :--- | :--- | :--- |
| `OnVitalStateChanged` | `bIsAlive`、`VitalTag` | 存活状态实际跃迁时 |
| `OnVitalStateChangedNative` | 同上 | 同上，C++ 原生多播 |

- 仅当存活状态发生变化时广播；`OnRep_VitalAlive` 与属性订阅回调通过 `bLastBroadcastedAlive` 去重，避免同帧双重广播。

## 类接口

```cpp
UCLASS(ClassGroup = (Game), meta = (BlueprintSpawnableComponent))
class GAMEGAMEPLAYCORE_API UGameVitalComponent : public UActorComponent, public IGamePoolable {
    GENERATED_BODY()

public:
    UGameVitalComponent();

    UFUNCTION(BlueprintCallable, Category = "Game|Vital")
    void BindStatComponent(UGameStatComponent* InStatComponent);

    UFUNCTION(BlueprintCallable, Category = "Game|Vital")
    void SetPrimaryVitalTag(const FGameplayTag& InTag);

    UFUNCTION(BlueprintPure, Category = "Game|Vital")
    [[nodiscard]] FORCEINLINE FGameplayTag GetPrimaryVitalTag() const noexcept;

    UFUNCTION(BlueprintPure, Category = "Game|Vital")
    [[nodiscard]] FORCEINLINE bool IsVitalAlive() const noexcept;

    UFUNCTION(BlueprintCallable, Category = "Game|Vital")
    void ReviveVital(float FillPercent = 1.0f);

    UFUNCTION(BlueprintCallable, Category = "Game|Vital")
    void EvaluateVitalState();

    UPROPERTY(BlueprintAssignable, Category = "Game|Vital")
    FOnGameVitalStateChanged OnVitalStateChanged;

    FOnGameVitalStateChangedNative OnVitalStateChangedNative;
};
```

| 函数 | 参数 | 返回 | 行为 |
| :--- | :--- | :--- | :--- |
| `BindStatComponent` | `InStatComponent` 目标数值组件；`nullptr` 显式解绑 | `void` | 解绑后不再自动回绑宿主数值组件 |
| `SetPrimaryVitalTag` | `InTag` 主生命标签 | `void` | 切换订阅并重新评估；同一标签直接返回；空标签恢复存活态 |
| `GetPrimaryVitalTag` | 无 | 当前主生命标签 | 未设置返回无效 Tag |
| `IsVitalAlive` | 无 | 存活布尔 | 未设置主生命标签时恒为 `true` |
| `ReviveVital` | `FillPercent` 填充比例，默认 `1.0f` | `void` | 恢复主生命数值并复活 |
| `EvaluateVitalState` | 无 | `void` | 依据当前派生值手动重评估 |

## 存活状态机

- **未设置主生命标签**：`bVitalAlive = true`，`EvaluateVitalState` 在需要时恢复存活态并广播。
- **已设置主生命标签**：`GetStatEffectiveCurrent(Tag) > GetStatMin(Tag)` 判定存活；否则 `bVitalAlive = false` 判定阵亡。
- **跃迁广播**：仅当新旧存活态不同时更新 `bVitalAlive` 并广播 `OnVitalStateChanged` / `OnVitalStateChangedNative`。
- **订阅驱动**：主生命属性变化经 `SubscribeStatChanged` 回调触发 `EvaluateVitalState`；数值组件缺失或属性未注册时保持原状态。
- **复制去重**：`bVitalAlive` 使用 `ReplicatedUsing = OnRep_VitalAlive`；客户端的 `OnRep` 仅在上次广播值不同时广播。

### 最小示例

```cpp
const FGameplayTag HealthTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Health"));
VitalComponent->BindStatComponent(StatComponent);
VitalComponent->SetPrimaryVitalTag(HealthTag);

const bool bAlive = VitalComponent->IsVitalAlive();

VitalComponent->ReviveVital(0.5f);
```

## 生命周期与自动绑定

- `BeginPlay` 检查订阅句柄是否已建立：即使构造期先调用 `BindStatComponent`、而 `PrimaryVitalTag` 来自蓝图 CDO 默认值（`EditDefaultsOnly`）后加载，也会自动补齐订阅。
- 显式 `BindStatComponent(nullptr)` 会解绑当前订阅并置位显式解绑状态，后续 `EvaluateVitalState` / `ReviveVital` 不会从 Owner 重绑。
- `EndPlay` 仅从原 `BoundStatComponent` 安全注销句柄，不触发额外组件查找。
- `SetPrimaryVitalTag` 先注销旧订阅，再按新标签重新订阅；标签未变化时直接返回。

## 复活与边界保护

- `ReviveVital` 依次校验：网络权威、`FMath::IsFinite(FillPercent)`、数值组件可解析、主生命标签有效、主生命属性已注册（底层 `GetStatValue` 失败，含 `StatNotFound 11003`）、`Max > Min`。
- 通过校验后按 `Min + (Max - Min) * Clamp(FillPercent, 0, 1)` 计算目标值并写入 `SetStatCurrent`；写入会触发属性订阅并重新评估存活态。
- 非权威端、非有限比例、未注册属性或非法区间均记录 `Warning` 后返回，不修改任何数值。

## 对象池契约

- `OnReleaseToPool` 仅静默重置本组件的 `bVitalAlive` 与 `bLastBroadcastedAlive`，遵循 SRP 不越权改写 `UGameStatComponent`。
- `OnAcquireFromPool` 在未显式解绑时补齐订阅，否则直接 `EvaluateVitalState` 对齐最新存活态。
- 专属日志分类 `LogGameVital`：正常流转 `Verbose`，异常边界 `Warning`。

## 常见错误与边界

- **忘记设置主生命标签**：组件恒为存活，`ReviveVital` 会因标签无效而空操作并记录 `Warning`。
- **非权威复活**：客户端调用 `ReviveVital` 被拒绝，仅输出限频 `Warning`，服务端状态不变。
- **比例越界**：`FillPercent` 会被钳制到 `[0, 1]`，负值等价于回填到 `Min`，大于 `1` 等价于回满；`NaN` / `±Inf` 直接拒绝。
- **非法区间**：主生命属性 `Max <= Min` 时复活被拒绝，需先修正数值配置。
- **显式解绑**：`BindStatComponent(nullptr)` 后不会自动回绑宿主组件，必须再次显式绑定。
- **入池语义**：入池只重置存活标志，不修改生命数值；数值回满由 `UGameStatComponent` 自行处理。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [通用数值组件 API](./GameStatComponent.md)
- [可受击接口 API](./IDamageable.md)
- [快速上手](../Guides/QuickStart.md)
