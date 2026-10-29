# GameGameplayCore 通用数值组件 API 参考

`UGameStatComponent` 是一个纯事件驱动、全 Tag 驱动注册、支持双模（自持/代理）自适应且支持网络同步的通用数值管理组件。

头文件：
- `#include "Stat/UGameStatComponent.h"`
- `#include "Stat/IGameStatValueSource.h"`

---

## 1. 核心结构体与枚举

### 1.1 `FGameStatValue`
通用属性配置与实时数值：
```cpp
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameStatValue {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    float Current = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    float Max = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    float Min = 0.0f;

    [[nodiscard]] float GetPercent() const noexcept;
};
```

### 1.2 `EGameStatSourceMode`
数值源驱动模式：
```cpp
UENUM(BlueprintType)
enum class EGameStatSourceMode : uint8 {
    Standalone, // 自持模式（组件内部管理与网络复制）
    Proxy       // 代理模式（外部数据源直通）
};
```

### 1.3 属性修正器与写操作
```cpp
UENUM(BlueprintType)
enum class EGameStatModifierOp : uint8 {
    Add,       // 加算
    Multiply,  // 乘算
    Override   // 覆写
};

USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameStatModifier {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    EGameStatModifierOp Op = EGameStatModifierOp::Add;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    float Value = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    bool bApplyToCurrent = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    bool bApplyToMax = true;

    FGameStatModifier() = default;
    explicit FGameStatModifier(float InValue, EGameStatModifierOp InOp = EGameStatModifierOp::Add, bool bInApplyToCurrent = true, bool bInApplyToMax = true);
    static FGameStatModifier MakeMultiplier(float InMultiplier, bool bInApplyToCurrent = true, bool bInApplyToMax = true);
};

UENUM(BlueprintType)
enum class EGameStatWriteOp : uint8 {
    Modify,      // 增量修改
    SetCurrent,  // 设置当前值
    SetMax       // 设置上限
};
```

### 1.4 代理读写委托
```cpp
DECLARE_DELEGATE_RetVal_OneParam(float, FGameStatQueryDelegate, const FGameplayTag&);
DECLARE_DELEGATE_RetVal_FourParams(
    bool, FGameStatWriteDelegate,
    const FGameplayTag&, EGameStatWriteOp, float, float&);
```

### 1.5 双轨多播委托定义
```cpp
// 属性变更委托（动态委托与 C++ 原生多播委托）
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
    FOnGameStatChanged,
    FGameplayTag, StatTag,
    float, OldValue,
    float, NewValue,
    float, MaxValue
);
DECLARE_MULTICAST_DELEGATE_FourParams(
    FOnGameStatChangedNative,
    const FGameplayTag&,
    float,
    float,
    float
);

// 主生命属性生死状态委托
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

## 2. `UGameStatComponent` 类接口

```cpp
UCLASS(ClassGroup = (Game), meta = (BlueprintSpawnableComponent))
class GAMEGAMEPLAYCORE_API UGameStatComponent : public UActorComponent {
    GENERATED_BODY()

public:
    // --- 属性注册与查询 ---
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void RegisterStat(const FGameplayTag& InStatTag, float InInitialCurrent, float InMax = 100.0f, float InMin = 0.0f);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    bool UnregisterStat(const FGameplayTag& InStatTag);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] bool HasStat(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void GetAllStatTags(TArray<FGameplayTag>& OutTags) const;

    // --- 统一查询接口 ---
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatCurrent(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatMax(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatMin(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatPercent(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    bool GetStatValue(const FGameplayTag& InStatTag, FGameStatValue& OutValue) const;

    // --- 修正器与派生数值 ---
    uint32 AddStatModifier(const FGameplayTag& InStatTag, const FGameStatModifier& InModifier);
    bool RemoveStatModifier(const FGameplayTag& InStatTag, uint32 InHandle);
    int32 ClearStatModifiers(const FGameplayTag& InStatTag);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatEffectiveCurrent(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatEffectiveMax(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatEffectivePercent(const FGameplayTag& InStatTag) const;

    // --- 统一修改接口 ---
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    bool ModifyStat(const FGameplayTag& InStatTag, float Delta, float& OutActualDelta);

    bool ModifyStat(const FGameplayTag& InStatTag, float Delta);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void SetStatCurrent(const FGameplayTag& InStatTag, float NewCurrent);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void SetStatMax(const FGameplayTag& InStatTag, float NewMax, bool bKeepRelativePercent = true);

    // --- 主生命状态（可选配置） ---
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void SetPrimaryVitalTag(const FGameplayTag& InTag);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] bool IsVitalAlive() const noexcept;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] FGameplayTag GetPrimaryVitalTag() const noexcept;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void ReviveVital(float FillPercent = 1.0f);

    // --- 外部代理接入 ---
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void BindStatSource(TScriptInterface<IGameStatValueSource> InSource);

    void BindNativeStatQueries(FGameStatQueryDelegate InCurrentQuery, FGameStatQueryDelegate InMaxQuery);

    void BindNativeStatWriter(FGameStatWriteDelegate InWriter);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void UnbindStatSource();

    UFUNCTION(BlueprintPure, Category = "Game|Stat")
    [[nodiscard]] bool CanWriteStat() const;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] FORCEINLINE EGameStatSourceMode GetSourceMode() const noexcept;

    // --- 按 Tag 订阅 ---
    FDelegateHandle SubscribeStatChanged(const FGameplayTag& InStatTag, FOnGameStatChangedNative::FDelegate InDelegate);
    FDelegateHandle SubscribeStatChanged(UObject* Owner, const FGameplayTag& InStatTag,
        TFunction<void(const FGameplayTag&, float, float, float)> InCallback);

    void UnsubscribeStatChanged(const FGameplayTag& InStatTag, FDelegateHandle InHandle);
};
```

---

### 写入语义

- **Standalone**：`ModifyStat` / `SetStatCurrent` / `SetStatMax` 直接修改内部值并按 `Min/Max` 钳制。
- **Proxy**：所有写操作经 `FGameStatWriteDelegate` 转发给外部数据源；未绑定写委托时 `ModifyStat` 返回 `false`，`SetStatCurrent` / `SetStatMax` 直接放弃写入（本地缓存不变）。
- **Proxy 代理写完整闭环**：外部写入生效后，无论该属性是否在组件内部表注册，组件均会重新计算修正器派生值；当派生当前值或派生上限发生变化时触发 `BroadcastStatChanged` 通知订阅者；若写入属性匹配 `PrimaryVitalTag`，自动评估生死状态并广播 `OnVitalStateChanged`。
- **双模复活支持**：`ReviveVital(FillPercent)`（默认 `1.0`）以 `Min + (Max - Min) × FillPercent` 计算目标值，并通过 `SetStatCurrent` 透传写入，无缝兼容自持模式与代理模式。
- **权威校验**：非权威端默认拒绝写入并记录 Warning；确需客户端预测时可在类默认值中开启 `bAllowNonAuthoritativeWrites`。
- 写入后组件回读外部源，`CanWriteStat` 可提前判断当前是否允许写入。

### 修正器与派生值

- 修正器按注册顺序叠加：先加算、再乘算、最后覆写；`GetStatEffective*` 返回叠加后的结果。
- 计算后强制执行 `FMath::Max(Calculated, MinVal)` 下限钳制，杜绝属性值被负向修正乘算为负值。
- 基础值（`GetStatCurrent/Max`）不受修正器影响，修正器只影响派生值与钳制结果。
- `AddStatModifier` 返回句柄；`RemoveStatModifier` 按句柄精确回滚，避免多 Buff 叠加时误删他人修正。
- 标签非法时 `AddStatModifier` 返回 0；属性尚未注册时修正器仍会被保存（此时不广播），待属性注册时随初始值广播一并计入派生值。

### 按 Tag 订阅

- `SubscribeStatChanged(InStatTag, Delegate)` 订阅时立即回放当前值（未注册属性也会保存订阅，注册时立即以初始值回放，之后随每次变化触发）。
- 成员函数订阅优先使用 `FOnGameStatChangedNative::FDelegate::CreateUObject(this, &UMyClass::OnStatChanged)`，引擎原生弱绑定即可随对象失效。
- `SubscribeStatChanged(Owner, InStatTag, Lambda)` 为 Owner 感知版本：条目持有 `TWeakObjectPtr`，Owner 销毁后广播自动跳过，失效条目在下次广播时清理；Owner 已失效时返回空句柄。
- 回调签名：`(StatTag, OldValue, NewValue, MaxValue)`。
- 退订使用 `UnsubscribeStatChanged` + 返回的 `FDelegateHandle`；旧订阅 API 行为不变。

---

## 3. 泛型属性源接口 `IGameStatValueSource`

当外部系统（如 GAS AttributeSet）维护具体数值时，外部持有者实现本接口即可无缝对接：

```cpp
UINTERFACE(MinimalAPI, BlueprintType, Blueprintable)
class UGameStatValueSource : public UInterface {
    GENERATED_BODY()
};

class GAMEGAMEPLAYCORE_API IGameStatValueSource {
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Game|Stat")
    float GetStatCurrent(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Game|Stat")
    float GetStatMax(const FGameplayTag& InStatTag) const;
};
```

