# GameGameplayCore 通用 Buff 组件 API 参考

`UGameBuffComponent` 是与 GAS 无关的通用 Buff / 状态运行时。框架不定义任何具体 Buff（中毒、加速、护盾、冷却等），只提供叠层、持续时长、状态标签聚合、原子属性修正器挂接与对象池复用。

- 头文件：`#include "Buff/UGameBuffComponent.h"`
- 实例结构头：`#include "Buff/FGameBuffInstance.h"`
- 错误码契约：`#include "Core/GameplayCoreErrorCodes.h"`（`Game::ModuleId::Gameplay` + `Game::GameplayCoreErrorCodes`）
- 依赖组件：`UGameStatComponent`（可在 Owner 上自动查找，或显式 `BindStatComponent` 绑定；传入 `nullptr` 将显式解绑且不再自动回绑宿主数值组件）
- 适用：非 GAS 项目直接使用；GAS 项目使用 `GameGameplay` 的 `UGameGasBuffBridge`

---

## 1. 数据结构

### 1.1 `FGameBuffSpec`
```cpp
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameBuffSpec {
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
- `Duration <= 0`：永久；`> 0`：按秒倒计时，到期自动移除；非有限浮点（`NaN` / `±Inf`）将被写边界拦截。
- `MaxStacks <= 0`：不限层。

### 1.2 `EGameBuffStackPolicy`
```cpp
enum class EGameBuffStackPolicy : uint8 {
    RefreshDuration,  // 刷新持续时长
    AddDuration,      // 累加持续时长
    Independent       // 独立实例
};
```

### 1.3 `FGameBuffInstance`（`Buff/FGameBuffInstance.h`）
```cpp
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameBuffInstance : public FFastArraySerializerItem {
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

---

## 2. 类接口

```cpp
UCLASS(ClassGroup = (Game), meta = (BlueprintSpawnableComponent))
class GAMEGAMEPLAYCORE_API UGameBuffComponent : public UActorComponent, public IGamePoolable {
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    void BindStatComponent(UGameStatComponent* InStatComponent);

    // --- 写入接口（C++ 强类型 TGameStatusResult + 蓝图 K2_* 门面） ---
    UFUNCTION(BlueprintCallable, Category = "Game|Buff", meta = (DisplayName = "Apply Buff"))
    int32 K2_ApplyBuff(const FGameBuffSpec& InSpec);
    TGameStatusResult<int32> ApplyBuff(const FGameBuffSpec& InSpec);

    UFUNCTION(BlueprintCallable, Category = "Game|Buff", meta = (DisplayName = "Remove Buff"))
    bool K2_RemoveBuff(const FGameplayTag& InBuffTag, bool bRemoveAllInstances = true);
    TGameStatusResult<void> RemoveBuff(const FGameplayTag& InBuffTag, bool bRemoveAllInstances = true);

    UFUNCTION(BlueprintCallable, Category = "Game|Buff", meta = (DisplayName = "Remove Buff Stacks"))
    bool K2_RemoveBuffStacks(const FGameplayTag& InBuffTag, int32 StacksToRemove = 1);
    TGameStatusResult<void> RemoveBuffStacks(const FGameplayTag& InBuffTag, int32 StacksToRemove = 1);

    UFUNCTION(BlueprintCallable, Category = "Game|Buff", meta = (DisplayName = "Remove Buff Instance"))
    bool K2_RemoveBuffInstance(int32 InInstanceId);
    TGameStatusResult<void> RemoveBuffInstance(int32 InInstanceId);

    UFUNCTION(BlueprintCallable, Category = "Game|Buff", meta = (DisplayName = "Clear All Buffs"))
    int32 K2_ClearAllBuffs();
    TGameStatusResult<int32> ClearAllBuffs();

    // --- 纯查询接口（全部 BlueprintPure） ---
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] bool HasBuff(const FGameplayTag& InBuffTag) const;

    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] bool HasGrantedTag(const FGameplayTag& InTag) const;

    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] int32 GetBuffStacks(const FGameplayTag& InBuffTag) const;

    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] float GetBuffRemainingTime(const FGameplayTag& InBuffTag) const;

    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    void GetActiveBuffTags(TArray<FGameplayTag>& OutTags) const;

    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    void GetGrantedTags(FGameplayTagContainer& OutTags) const;

    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] bool IsAuthoritativeWriteAllowed() const;

    // --- 动静双轨多播委托 ---
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
- `RefreshDuration` / `AddDuration`：同 Tag 复用同一实例；广播始终采用该 Tag 的聚合总层数 `GetBuffStacks(Tag)`。
- `AddDuration`：新 Spec 为正时长时累加剩余时间；非正时长时将实例转为永久，剩余时间归零。`RefreshDuration` 始终刷新为新时长。
- `Independent`：每次施加创建独立实例（受 `MaxStacks` 限制：达到上限时返回 `MaxBuffStacksReached`），按 `InstanceId` 精确移除。
- `HasGrantedTag(Tag)` 直接短路遍历活跃 Buff 的 `GrantedTags`（支持父标签层级匹配），无需构造临时 `FGameplayTagContainer`。

### 3.2 原子修正器挂接（零刷新抖动、防瞬时误杀）
- `Add` 修正值乘以层数；`Multiply` 按 `1 + (Value - 1) * Stacks` 缩放；`Override` 不缩放；规格中的无效 `StatTag` 或 `NaN` 修正器会被自动过滤。
- 通过 `FGameBuffModifierBridge` 与 `UGameStatComponent::FScopedModifierBatch` / `UpdateStatModifier` 协同：
  - **满层刷新（缩放后修正器完全不变）**：零操作直接跳过，不触发任何属性广播；
  - **叠层/扣层（同属性标签布局）**：原地原子更新修正器，仅产生一次增量广播，绝不回落至基础值（防止靠生命 Buff 存活的角色在刷新或叠层瞬间误触发死亡）；
  - **Spec 变更/重绑/批量清空**：在批处理作用域内完成替换与回滚，退出时按终态差分统一广播。

### 3.3 持续时长与 Tick 自适应
- 仅当存在未到期的限时实例时组件才开启 Tick；全部永久 Buff 或客户端 `PreReplicatedRemove` 移除最后一条限时实例时**当帧立即关闭 Tick**。
- `RemoveBuffStacks` 仅在实际删除实例时标脏整表（`MarkArrayDirty`）；仅部分扣层时只标脏单条目（`MarkItemDirty`），维持 FastArray 增量带宽优势。

### 3.4 权威校验、复制差分与对象池
- `IsAuthoritativeWriteAllowed()` 与 `UGameStatComponent` 对齐：Owner 为 `nullptr`、Owner 具备权威或开启 `bAllowNonAuthoritativeWrites` 时允许写入。
- 客户端 `AcknowledgedStacks` 与复制差分基于 `GameCore::TSortedVectorMap<FGameFastTagKey, int32, 8>`，8 槽内联零堆分配 + 整型快速二分。
- `OnReleaseToPool` 静默清空 `ActiveBuffs`、关闭 Tick、通过静默回滚通道卸载已挂接修正器（无论与 `UGameStatComponent` 的入池顺序如何均零业务广播）并保持 `NextInstanceId` 单调递增（防联机延迟包 ID 碰撞）。
- 专属子域日志分类为 `LogGameBuff`（正常流转记录 `Verbose`，非法输入或非权威写记录限频 `Warning`）。

---

## 4. 事件

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGameBuffChanged, FGameplayTag, BuffTag, int32, Stacks);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameBuffRemoved, FGameplayTag, BuffTag);

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnGameBuffChangedNative, const FGameplayTag&, int32);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnGameBuffRemovedNative, const FGameplayTag&);
```

- Blueprint 与 C++ 双轨；C++ 侧建议 `AddWeakLambda` 绑定拥有者。
