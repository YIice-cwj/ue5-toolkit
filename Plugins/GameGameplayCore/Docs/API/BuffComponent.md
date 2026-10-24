# GameGameplayCore 通用 Buff 组件 API 参考

`UGameBuffComponent` 是与 GAS 无关的通用 Buff / 状态运行时。框架不定义任何具体 Buff（中毒、加速、护盾、冷却等），只提供叠层、持续时长、状态标签聚合与属性修正器挂接。

- 头文件：`#include "Combat/UGameBuffComponent.h"`
- 依赖组件：`UGameStatComponent`（可在 Owner 上自动查找，或显式 `BindStatComponent` 绑定）
- 适用：非 GAS 项目直接使用；GAS 项目使用 `GameGameplay` 的 `UGameGasBuffBridge`

---

## 1. 数据结构

### 1.1 `FGameBuffSpec`
```cpp
USTRUCT(BlueprintType)
struct FGameBuffSpec {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    FGameplayTag BuffTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    FGameplayTagContainer GrantedTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    float Duration = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    int32 MaxStacks = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    EGameBuffStackPolicy StackPolicy = EGameBuffStackPolicy::RefreshDuration;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    TArray<FGameStatModifierSpec> Modifiers;
};
```

- `BuffTag`：Buff 身份；同 Tag 视为同一种 Buff。
- `GrantedTags`：激活期间对外聚合的状态标签（眩晕、无敌、冷却中）。
- `Duration <= 0`：永久；`> 0`：按秒倒计时，到期自动移除。
- `MaxStacks <= 0`：不限层。

### 1.2 `EGameBuffStackPolicy`
```cpp
enum class EGameBuffStackPolicy : uint8 {
    RefreshDuration,  // 刷新持续时长
    AddDuration,      // 累加持续时长
    Independent       // 独立实例
};
```

### 1.3 `FGameStatModifierSpec`
```cpp
USTRUCT(BlueprintType)
struct FGameStatModifierSpec {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    FGameplayTag StatTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    FGameStatModifier Modifier;
};
```

### 1.4 `FGameBuffInstance`
```cpp
USTRUCT(BlueprintType)
struct FGameBuffInstance : public FFastArraySerializerItem {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Game|Buff")
    int32 InstanceId = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Game|Buff")
    FGameBuffSpec Spec;

    UPROPERTY(BlueprintReadOnly, Category = "Game|Buff")
    int32 Stacks = 1;

    UPROPERTY(BlueprintReadOnly, Category = "Game|Buff")
    float RemainingSeconds = 0.0f;
};
```

### 1.5 `FGameActiveBuffArray`
```cpp
USTRUCT()
struct FGameActiveBuffArray : public FFastArraySerializer {
    GENERATED_BODY()

    UPROPERTY()
    TArray<FGameBuffInstance> Items;

    void PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize);
    void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);
    void PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize);
};
```

---

## 2. 类接口

```cpp
UCLASS(ClassGroup = (Game), meta = (BlueprintSpawnableComponent))
class GAMEGAMEPLAYCORE_API UGameBuffComponent : public UActorComponent {
    GENERATED_BODY()

public:
    void BindStatComponent(UGameStatComponent* InStatComponent);

    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    int32 ApplyBuff(const FGameBuffSpec& InSpec, UObject* InSource = nullptr);

    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    bool RemoveBuff(FGameplayTag InBuffTag, bool bRemoveAllInstances = true);

    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    bool RemoveBuffStacks(FGameplayTag InBuffTag, int32 StacksToRemove = 1);

    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    bool RemoveBuffInstance(int32 InInstanceId);

    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    bool HasBuff(FGameplayTag InBuffTag) const;

    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    int32 GetBuffStacks(FGameplayTag InBuffTag) const;

    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    float GetBuffRemainingTime(FGameplayTag InBuffTag) const;

    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    void GetActiveBuffTags(TArray<FGameplayTag>& OutTags) const;

    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    void GetGrantedTags(FGameplayTagContainer& OutTags) const;

    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    bool IsApplyAllowed() const;

    UPROPERTY(BlueprintAssignable, Category = "Game|Buff")
    FOnGameBuffChanged OnBuffChanged;

    UPROPERTY(BlueprintAssignable, Category = "Game|Buff")
    FOnGameBuffRemoved OnBuffRemoved;

    FOnGameBuffChangedNative OnBuffChangedNative;
    FOnGameBuffRemovedNative OnBuffRemovedNative;
};
```

---

## 3. 运行语义

### 3.1 施加与叠层
- `RefreshDuration` / `AddDuration`：同 Tag 复用同一实例；每层叠加时先移除旧修正器，再按新层数重新挂接。
- `AddDuration` 且新 Spec 有时长时累加剩余时间；否则刷新为新时长。
- `Independent`：每次施加创建独立实例（受 `MaxStacks` 限制：同 Tag 聚合总层数达到 `MaxStacks` 时拒绝施加并返回 0），按 `InstanceId` 精确移除。
- 层数达到 `MaxStacks` 后不再增加，但仍刷新 / 累加时长并更新 Spec。

### 3.2 修正器与层数
- `Add` 修正值乘以层数；`Multiply` 按 `1 + (Value - 1) * Stacks` 缩放；`Override` 不缩放。
- 修正器挂接到 `UGameStatComponent::AddStatModifier`，移除 Buff 时按句柄精确回滚，不影响其他来源的修正。

### 3.3 持续时长
- 仅当存在限时实例时组件才开启 Tick；全部永久 Buff 时自动关闭 Tick。
- 到期实例先回滚修正器，再移除并广播 `OnBuffRemoved`。

### 3.4 权威与复制
- `IsApplyAllowed()`：Owner 为 `nullptr`、Owner 具备权威、或关闭 `bRequireAuthority` 时允许操作。
- 非权威端 `ApplyBuff` / `RemoveBuff` / `RemoveBuffInstance` 返回失败并记录 Warning。
- `ActiveBuffs` 为 `FGameActiveBuffArray`（`FFastArraySerializer`），服务端仅标记脏条目，客户端按 `PostReplicatedAdd` / `PostReplicatedChange` / `PreReplicatedRemove` 回调重建修正器映射，并按 `AcknowledgedStacks` 差分广播变化 / 移除事件。
- `GetBuffStacks` 对同 Tag 多实例求和；`GetBuffRemainingTime` 返回最大剩余时间。

### 3.5 逐层削减 (`RemoveBuffStacks`)
- 按 Buff 标签从后向前削减指定层数。
- 若实例层数小于等于待削减层数，直接移除该实例；若大于待削减层数，则扣除层数并按剩余层数重新缩放修正器。
- 削减后若该 Tag 仍有剩余层数，广播 `OnBuffChanged`；若全部层数归零，广播 `OnBuffRemoved`。

---

## 4. 事件

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGameBuffChanged, FGameplayTag, BuffTag, int32, Stacks);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameBuffRemoved, FGameplayTag, BuffTag);

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnGameBuffChangedNative, const FGameplayTag&, int32);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnGameBuffRemovedNative, const FGameplayTag&);
```

- Blueprint 与 C++ 双轨；C++ 侧建议 `AddWeakLambda` 绑定拥有者。
