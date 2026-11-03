# GameGameplayCore 实体生命与生死状态管理组件 API 参考

`UGameVitalComponent` 是实体生命与生死状态管理的专职组件（单一职责设计）。它负责主生命属性绑定、存活状态判定、致死判定与复活状态机，并与 `UGameStatComponent` 协同工作。

头文件：
- `#include "Stat/UGameVitalComponent.h"`

---

## 1. 核心委托定义

```cpp
// 动态多播委托：存活状态或主生命标签发生变动（供蓝图绑定）
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnGameVitalStateChanged,
    bool, bIsAlive,
    const FGameplayTag&, VitalTag
);

// 原生 C++ 多播委托：存活状态或主生命标签发生变动（零 UObject 开销）
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
class GAMEGAMEPLAYCORE_API UGameVitalComponent : public UActorComponent {
    GENERATED_BODY()

public:
    UGameVitalComponent();

    // --- 关联数值组件绑定 ---
    UFUNCTION(BlueprintCallable, Category = "Game|Vital")
    void BindStatComponent(UGameStatComponent* InStatComponent);

    // --- 主生命标签与状态查询 ---
    UFUNCTION(BlueprintCallable, Category = "Game|Vital")
    void SetPrimaryVitalTag(const FGameplayTag& InTag);

    UFUNCTION(BlueprintCallable, Category = "Game|Vital")
    [[nodiscard]] FORCEINLINE FGameplayTag GetPrimaryVitalTag() const noexcept;

    UFUNCTION(BlueprintCallable, Category = "Game|Vital")
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

1. **自动挂接与解绑**：
   - 若未显式调用 `BindStatComponent`，组件在 `BeginPlay` 时会自动尝试从 Owner Actor 查找 `UGameStatComponent`；
   - 切换绑定或组件销毁（`EndPlay`）时，自动注销原数值组件上的属性监听句柄，防止悬垂回调。
2. **存活判定逻辑**：
   - 未指定 `PrimaryVitalTag` 时默认恒为存活状态 (`bVitalAlive = true`)；
   - 指定标签后，当该属性派生当前值 `GetStatEffectiveCurrent(Tag) > GetStatMin(Tag)` 时判定为存活，否则判定为阵亡 (`bVitalAlive = false`)。
3. **网络复制与去重**：
   - `bVitalAlive` 开启网络复制 (`ReplicatedUsing = OnRep_VitalAlive`)；
   - 客户端内部具有广播去重机制，避免数值同步与布尔位同步在同帧到达时引起双重广播。
4. **复活逻辑**：
   - `ReviveVital(FillPercent)` 会按 `[Min, Max]` 区间恢复对应比例数值，并立即触发存活状态跃迁与广播。

