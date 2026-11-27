# GameGameplayCore 实体生命与生死状态管理组件 API 参考

`UGameVitalComponent` 是实体生命与生死状态管理的专职组件（单一职责设计）。它负责主生命属性绑定、存活状态判定、致死判定、复活状态机与对象池静默重置，并与 `UGameStatComponent` 协同工作。

头文件：
- `#include "Stat/UGameVitalComponent.h"`

---

## 1. 核心委托定义

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

---

## 2. `UGameVitalComponent` 类接口

```cpp
UCLASS(ClassGroup = (Game), meta = (BlueprintSpawnableComponent))
class GAMEGAMEPLAYCORE_API UGameVitalComponent : public UActorComponent, public IGamePoolable {
    GENERATED_BODY()

public:
    UGameVitalComponent();

    // --- 关联数值组件绑定 ---
    UFUNCTION(BlueprintCallable, Category = "Game|Vital")
    void BindStatComponent(UGameStatComponent* InStatComponent);

    // --- 主生命标签与状态查询 ---
    UFUNCTION(BlueprintCallable, Category = "Game|Vital")
    void SetPrimaryVitalTag(const FGameplayTag& InTag);

    UFUNCTION(BlueprintPure, Category = "Game|Vital")
    [[nodiscard]] FORCEINLINE FGameplayTag GetPrimaryVitalTag() const noexcept;

    UFUNCTION(BlueprintPure, Category = "Game|Vital")
    [[nodiscard]] FORCEINLINE bool IsVitalAlive() const noexcept;

    // --- 复活与手动重评估 ---
    UFUNCTION(BlueprintCallable, Category = "Game|Vital")
    void ReviveVital(float FillPercent = 1.0f);

    UFUNCTION(BlueprintCallable, Category = "Game|Vital")
    void EvaluateVitalState();

    // --- 委托广播 ---
    UPROPERTY(BlueprintAssignable, Category = "Game|Vital")
    FOnGameVitalStateChanged OnVitalStateChanged;

    FOnGameVitalStateChangedNative OnVitalStateChangedNative;
};
```

---

## 3. 核心机制说明

1. **自动挂接、CDO 预配置与显式解绑**：
   - `BeginPlay` 检查订阅句柄是否已建立：即便构造期已先调用 `BindStatComponent`、而 `PrimaryVitalTag` 是由蓝图 CDO 默认值（`EditDefaultsOnly`）后加载的，也能在 `BeginPlay` 自动补齐订阅；
   - 显式调用 `BindStatComponent(nullptr)` 会解绑当前订阅并置位显式解绑状态，后续 `EvaluateVitalState` / `ReviveVital` 不会偷偷从 Owner 重绑回来；
   - `EndPlay` 仅从原 `BoundStatComponent` 安全注销句柄，不触发额外组件查找。
2. **存活判定逻辑**：
   - 未指定 `PrimaryVitalTag` 时默认恒为存活状态（`bVitalAlive = true`）；
   - 指定标签后，当该属性派生当前值 `GetStatEffectiveCurrent(Tag) > GetStatMin(Tag)` 时判定为存活，否则判定为阵亡（`bVitalAlive = false`）。
3. **网络复制与去重**：
   - `bVitalAlive` 开启网络复制（`ReplicatedUsing = OnRep_VitalAlive`）；
   - 客户端内部维护 `bLastBroadcastedAlive` 去重，避免属性同步与存活布尔同步在同帧引起双重广播。
4. **复活与边界保护**：
   - `ReviveVital(FillPercent)` 强制校验网络权威、`FMath::IsFinite(FillPercent)`、主生命属性已注册及 `Max > Min`，按 `Min + (Max - Min) * Clamp(FillPercent, 0, 1)` 恢复数值。
5. **对象池静默重置 (`IGamePoolable`)**：
   - 遵循 SRP，`OnReleaseToPool` 仅静默重置本组件的 `bVitalAlive` 与 `bLastBroadcastedAlive`（不越权改写 `UGameStatComponent`）；出库 `OnAcquireFromPool` 时自动补齐订阅并对齐最新存活态。
   - 专属子域日志分类为 `LogGameVital`（正常流转记录 `Verbose`，异常边界记录 `Warning`）。
