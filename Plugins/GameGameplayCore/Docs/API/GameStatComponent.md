# GameGameplayCore 通用数值组件 API 参考

`UGameStatComponent` 是一个纯事件驱动、全 Tag 驱动注册、支持网络同步与对象池复用的通用数值管理组件。

> **架构说明**：角色主生命标签、存活判定（`IsVitalAlive`）与复活（`ReviveVital`）已按照 SRP 原则解耦至专职组件 [UGameVitalComponent](./GameVitalComponent.md)。

头文件：
- `#include "Stat/UGameStatComponent.h"`
- `#include "Core/GameplayCoreErrorCodes.h"`（错误码常量：`Game::ModuleId::Gameplay` + `Game::GameplayCoreErrorCodes`）

---

## 1. 核心结构体、句柄与枚举

### 1.1 `FGameStatModifierHandle`
基于 `GameCore` 的 64 位强类型代际句柄（32 位序号 + 32 位代际号），防止与其他模块句柄混用，并在对象池重置后自动使历史句柄失效（杜绝 ABA 误删）：
```cpp
struct FStatModifierHandleTag {};
using FGameStatModifierHandle = TGameHandle<FStatModifierHandleTag>;
```

### 1.2 `FGameStatValue`
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

    [[nodiscard]] static FORCEINLINE float EvaluatePercent(float InCurrent, float InMax, float InMin) noexcept;
    [[nodiscard]] FORCEINLINE float GetPercent() const noexcept;
    [[nodiscard]] bool operator==(const FGameStatValue& Other) const noexcept;
};
```

### 1.3 属性修正器
```cpp
UENUM(BlueprintType)
enum class EGameStatModifierOp : uint8 {
    Add,       // 加算
    Multiply,  // 乘算
    Override   // 覆写（按插入顺序后者优先）
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
    [[nodiscard]] bool operator==(const FGameStatModifier& Other) const noexcept;
    static FGameStatModifier MakeMultiplier(float InMultiplier, bool bInApplyToCurrent = true, bool bInApplyToMax = true);
};
```

### 1.4 双轨多播委托定义
```cpp
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
```

---

## 2. `UGameStatComponent` 类接口

```cpp
UCLASS(ClassGroup = (Game), meta = (BlueprintSpawnableComponent))
class GAMEGAMEPLAYCORE_API UGameStatComponent : public UActorComponent, public IGamePoolable {
    GENERATED_BODY()

public:
    // --- RAII 修正器批处理守卫（合并作用域内多次增删改为单次终态差分广播） ---
    class GAMEGAMEPLAYCORE_API FScopedModifierBatch;

    // --- 属性注册与注销 ---
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void RegisterStat(const FGameplayTag& InStatTag, float InInitialCurrent, float InMax = 100.0f, float InMin = 0.0f);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat", meta = (DisplayName = "Unregister Stat"))
    bool K2_UnregisterStat(const FGameplayTag& InStatTag);
    TGameStatusResult<void> UnregisterStat(const FGameplayTag& InStatTag);

    // --- 纯查询接口（全部 BlueprintPure） ---
    UFUNCTION(BlueprintPure, Category = "Game|Stat")
    [[nodiscard]] bool HasStat(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintPure, Category = "Game|Stat")
    void GetAllStatTags(TArray<FGameplayTag>& OutTags) const;

    UFUNCTION(BlueprintPure, Category = "Game|Stat")
    [[nodiscard]] float GetStatCurrent(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintPure, Category = "Game|Stat")
    [[nodiscard]] float GetStatMax(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintPure, Category = "Game|Stat")
    [[nodiscard]] float GetStatMin(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintPure, Category = "Game|Stat")
    [[nodiscard]] float GetStatPercent(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat", meta = (DisplayName = "Get Stat Value"))
    bool K2_GetStatValue(const FGameplayTag& InStatTag, FGameStatValue& OutValue) const;
    [[nodiscard]] TGameStatusResult<FGameStatValue> GetStatValue(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintPure, Category = "Game|Stat")
    [[nodiscard]] bool IsAuthoritativeWriteAllowed() const;

    // --- 修正器与派生数值（本地派生层） ---
    UFUNCTION(BlueprintCallable, Category = "Game|Stat", meta = (DisplayName = "Add Stat Modifier"))
    int64 K2_AddStatModifier(const FGameplayTag& InStatTag, const FGameStatModifier& InModifier);
    TGameStatusResult<FGameStatModifierHandle> AddStatModifier(const FGameplayTag& InStatTag, const FGameStatModifier& InModifier);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat", meta = (DisplayName = "Update Stat Modifier"))
    bool K2_UpdateStatModifier(const FGameplayTag& InStatTag, int64 InPackedHandle, const FGameStatModifier& InNewModifier);
    TGameStatusResult<void> UpdateStatModifier(const FGameplayTag& InStatTag, FGameStatModifierHandle InHandle, const FGameStatModifier& InNewModifier);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat", meta = (DisplayName = "Remove Stat Modifier"))
    bool K2_RemoveStatModifier(const FGameplayTag& InStatTag, int64 InPackedHandle);
    TGameStatusResult<void> RemoveStatModifier(const FGameplayTag& InStatTag, FGameStatModifierHandle InHandle);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    int32 ClearStatModifiers(const FGameplayTag& InStatTag);

    UFUNCTION(BlueprintPure, Category = "Game|Stat")
    [[nodiscard]] float GetStatEffectiveCurrent(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintPure, Category = "Game|Stat")
    [[nodiscard]] float GetStatEffectiveMax(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintPure, Category = "Game|Stat")
    [[nodiscard]] float GetStatEffectivePercent(const FGameplayTag& InStatTag) const;

    // --- 统一修改接口 ---
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    bool ModifyStat(const FGameplayTag& InStatTag, float Delta, float& OutActualDelta);
    bool ModifyStat(const FGameplayTag& InStatTag, float Delta);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void SetStatCurrent(const FGameplayTag& InStatTag, float NewCurrent);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void SetStatMax(const FGameplayTag& InStatTag, float NewMax, bool bKeepRelativePercent = true);

    // --- 按 Tag 订阅 ---
    FDelegateHandle SubscribeStatChanged(const FGameplayTag& InStatTag, FOnGameStatChangedNative::FDelegate InDelegate);
    FDelegateHandle SubscribeStatChanged(UObject* Owner, const FGameplayTag& InStatTag,
        TFunction<void(const FGameplayTag&, float, float, float)> InCallback);
    void UnsubscribeStatChanged(const FGameplayTag& InStatTag, FDelegateHandle InHandle);
};
```

---

## 3. 核心语义与契约

### 3.1 写入、权威校验与数值安全
- 所有公开浮点写入口（`RegisterStat` / `ModifyStat` / `SetStatCurrent` / `SetStatMax` / `AddStatModifier` / `UpdateStatModifier`）均在边界强制拦截 `NaN` / `±Inf` 非有限浮点，防止属性毒化或错误满血。
- 非权威端默认拒绝修改基础值并输出限频 `Warning`（日志分类 `LogGameStat`）；确需客户端预测时可在类默认值开启 `bAllowNonAuthoritativeWrites`。
- 强类型接口失败统一返回 `Game::ModuleId::Gameplay` 与 `Game::GameplayCoreErrorCodes::*`（`InvalidParameter` / `NotAuthority` / `StatNotFound` / `ModifierHandleNotFound`）。
- `UnregisterStat` 使用 `RemoveAtSwap` + $O(1)$ 索引同步移除属性，并级联清理修正器、订阅者与已确认差分记录。

### 3.2 修正器、批处理与派生值
- 修正器属于本地派生层（不直接网络复制，客户端通过 Buff/装备同步各自重建），按注册顺序叠加：**先加算 → 再乘算 → 最后覆写（后注册者覆盖先注册者）**，最后强制与 `Min` 执行 `FMath::Max` 下限钳制。
- `AddStatModifier` 返回 `TGameStatusResult<FGameStatModifierHandle>`；`UpdateStatModifier` 原地原子更新单句柄修正器；配合 `UGameStatComponent::FScopedModifierBatch` 可将一组修正器增删改合并为单次终态差分广播，杜绝中间态回落抖动。

### 3.3 重入安全的按 Tag 订阅
- 两种 `SubscribeStatChanged` 重载均会在属性已注册时**仅向本次新订阅者**立即回放当前有效值。
- 内部 `FGameStatSubscriptionHub` 采用 `TSharedRef` 广播期保活 + UE 原生 `CreateWeakLambda` 弱引用绑定，免疫回调内自退订、大规模新订阅导致的 `TMap` 重排及中途销毁其他 Owner。

### 3.4 对象池契约 (`IGamePoolable`)
- `OnReleaseToPool` 静默清空修正器（并递增修正器代际号使历史句柄全部失效）、将全部已注册属性回满至 `Max` 并对齐 `AcknowledgedDiff`，**不向外广播业务事件**（杜绝死亡实体入池误触发复活事件）。
