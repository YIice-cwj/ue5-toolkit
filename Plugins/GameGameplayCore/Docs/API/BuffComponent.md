# GameGameplayCore 通用 Buff 组件 API 参考

> **一句话**：与 GAS 无关的通用 Buff / 状态运行时，提供叠层、持续时长、状态标签聚合与属性修正器挂接。
> **头文件**：`Buff/UGameBuffComponent.h`、`Buff/FGameBuffInstance.h`、`Core/GameplayCoreErrorCodes.h` · **线程**：写入、查询与委托回调均限 GameThread；客户端仅执行本地倒计时
> **失败语义**：C++ 接口返回 `TGameStatusResult<T>` + `Game::GameplayCoreErrorCodes`；`K2_*` 门面将失败折叠为 `0` / `false`

## 30 秒速览

- **不预置具体 Buff**：`FGameBuffSpec` 用 `BuffTag` 定义身份，用 `GrantedTags`、`Duration`、`MaxStacks`、`StackPolicy`、`Modifiers` 描述行为。
- **叠层**：`RefreshDuration` / `AddDuration` 复用同 Tag 实例并广播聚合总层数；`Independent` 每次施加创建独立实例，按 `InstanceId` 精确移除。
- **修正器**：按层数缩放挂接；满层刷新零操作，叠层 / 扣层原地原子更新，Spec 变更、重绑与批量清空在批处理作用域内完成替换与回滚。
- **复制**：`ActiveBuffs` 走 FastArray 增量复制，客户端按已确认层数做差分广播；对象池归还全程静默。
- **权威**：默认仅服务端可写，可由 `bAllowNonAuthoritativeWrites` 放开客户端预测。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `UGameBuffComponent::ApplyBuff` | 施加或刷新 Buff，返回实例 ID | 仅 GameThread；失败返回 `11001` / `11002` / `11006` |
| `UGameBuffComponent::RemoveBuff` | 按身份标签移除实例 | 仅 GameThread；未命中返回 `11005` |
| `UGameBuffComponent::RemoveBuffStacks` | 按身份标签扣减层数 | 仅 GameThread；未命中返回 `11005` |
| `UGameBuffComponent::RemoveBuffInstance` | 按 `InstanceId` 精确移除单个实例 | 仅 GameThread；未命中返回 `11005` |
| `UGameBuffComponent::ClearAllBuffs` | 清空全部 Buff，返回被清除实例数 | 仅 GameThread；非权威返回 `11002` |
| `UGameBuffComponent::HasBuff` | 查询是否存在活跃实例 | 只读 `BlueprintPure`，无副作用 |
| `UGameBuffComponent::GetBuffStacks` | 查询该 Tag 全部实例的层数总和 | 无实例返回 `0` |
| `UGameBuffComponent::HasGrantedTag` | 查询聚合状态标签是否命中（含父标签层级） | 只读，直接短路遍历不构造临时容器 |

## 错误码

全部归属 `Game::ModuleId::Gameplay`，由 `Core/GameplayCoreErrorCodes.h` 定义。

| 常量 | 数值 | 触发条件 | 典型接口 |
| :--- | :---: | :--- | :--- |
| `InvalidParameter` | `11001` | 无效 `GameplayTag`、`NaN` / `±Inf` 持续时长或非正数层数 | `ApplyBuff` / `RemoveBuffStacks` / `RemoveBuffInstance` |
| `NotAuthority` | `11002` | 非网络权威端且未开启 `bAllowNonAuthoritativeWrites` | 全部写入接口 |
| `StatNotFound` | `11003` | 目标属性标签未在数值组件注册 | `UGameStatComponent` |
| `ModifierHandleNotFound` | `11004` | 修正器句柄无效或代际已过期 | `UGameStatComponent` |
| `BuffNotFound` | `11005` | 目标 Buff 标签或运行实例 ID 不存在 | `RemoveBuff` / `RemoveBuffStacks` / `RemoveBuffInstance` |
| `MaxBuffStacksReached` | `11006` | `Independent` 策略下该 Buff 已达 `MaxStacks` 上限 | `ApplyBuff` |

本组件实际返回 `11001` / `11002` / `11005` / `11006`；`11003` / `11004` 由 [UGameStatComponent](./GameStatComponent.md) 返回。

## 数据结构

### `FGameBuffSpec`

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

| 字段 | 说明 |
| :--- | :--- |
| `BuffTag` | Buff 身份；同 Tag 视为同一种 Buff，用于叠层与查询 |
| `GrantedTags` | 激活期间对外聚合的状态标签（如 `Status.Stun`、`Status.Invulnerable`） |
| `Duration` | `<= 0` 永久；`> 0` 按秒倒计时，到期自动移除；非有限浮点被写入边界拦截 |
| `MaxStacks` | `<= 0` 不限层；`RefreshDuration` / `AddDuration` 下达到上限后不再加层，`Independent` 下超限返回 `11006` |
| `StackPolicy` | 重复施加时的叠层策略 |
| `Modifiers` | 激活期间挂接到 `UGameStatComponent` 的属性修正器列表；无效 `StatTag` 或 `NaN` 修正值会被过滤 |

### `EGameBuffStackPolicy`

```cpp
enum class EGameBuffStackPolicy : uint8 {
    RefreshDuration,
    AddDuration,
    Independent
};
```

| 枚举值 | 行为 |
| :--- | :--- |
| `RefreshDuration` | 复用同 Tag 实例，层数加 1（至上限），剩余时长刷新为 Spec 的 `Duration` |
| `AddDuration` | 复用同 Tag 实例，层数加 1（至上限），剩余时长追加 Spec 的 `Duration` |
| `Independent` | 每次施加创建互不干扰的独立计时实例，层数各自独立，受 `MaxStacks` 限制 |

### `FGameBuffInstance`

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

- 运行实例为 FastArray 复制条目；`InstanceId` 单调递增分配，用于按实例精确移除与修正器句柄映射。

## 写入接口

```cpp
UFUNCTION(BlueprintCallable, Category = "Game|Buff")
void BindStatComponent(UGameStatComponent* InStatComponent);

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
```

| 函数 | 参数 | 返回 | 失败 |
| :--- | :--- | :--- | :--- |
| `BindStatComponent` | `InStatComponent` 目标数值组件；`nullptr` 表示显式解绑 | `void` | 解绑后不再自动回绑宿主数值组件 |
| `ApplyBuff` | `InSpec` Buff 描述 | 生成或刷新的实例 ID | `11001` / `11002` / `11006` |
| `RemoveBuff` | `InBuffTag` 身份标签；`bRemoveAllInstances` 是否移除全部实例 | `Success` | `11001` / `11002` / `11005` |
| `RemoveBuffStacks` | `InBuffTag`；`StacksToRemove` 扣减层数 | `Success` | `11001` / `11002` / `11005` |
| `RemoveBuffInstance` | `InInstanceId` 运行实例 ID | `Success` | `11001` / `11002` / `11005` |
| `ClearAllBuffs` | 无 | 被清除的实例总数 | `11002` |

- **线程 / 生命周期**：仅 GameThread 调用；`BindStatComponent` 会回滚旧组件上的修正器并将活跃 Buff 迁移至新组件；`EndPlay` 回滚全部已挂接修正器。
- **失败语义**：`K2_*` 门面丢弃错误码，`ApplyBuff` 失败返回 `0`，其余布尔门面返回 `false`。
- **无数值组件**：未绑定且宿主上不存在 `UGameStatComponent` 时，Buff 的叠层、时长与标签仍运行，仅属性修正器不落地。

### 最小示例

```cpp
FGameBuffSpec Poison;
Poison.BuffTag = FGameplayTag::RequestGameplayTag(TEXT("Buff.Debuff.Poison"));
Poison.GrantedTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("State.Poisoned")));
Poison.Duration = 6.0f;
Poison.MaxStacks = 5;
Poison.StackPolicy = EGameBuffStackPolicy::AddDuration;

const TGameStatusResult<int32> ApplyResult = BuffComponent->ApplyBuff(Poison);
const int32 InstanceId = ApplyResult.IsSuccess() ? ApplyResult.GetValue() : 0;

BuffComponent->RemoveBuffStacks(Poison.BuffTag, 2);
```

## 叠层、时长与 Tick

- `RefreshDuration` / `AddDuration` 复用同一 Tag 实例，变更广播始终使用该 Tag 的聚合总层数 `GetBuffStacks(Tag)`。
- `AddDuration` 在新 Spec 时长为正时累加剩余时间；时长非正时将实例转为永久并把剩余时间归零。`RefreshDuration` 始终把剩余时间刷新为新时长。
- `Independent` 每次施加创建独立实例；达到 `MaxStacks` 返回 `MaxBuffStacksReached (11006)`，按 `InstanceId` 精确移除。
- 仅当存在未到期的限时实例时组件才开启 Tick；全部为永久 Buff，或客户端在 `PreReplicatedRemove` 中移除最后一条限时实例时，当帧关闭 Tick。
- `RemoveBuffStacks` 在实际删除实例时标脏整表（`MarkArrayDirty`）；仅部分扣层时只标脏单条目（`MarkItemDirty`），保持 FastArray 增量带宽优势。

## 属性修正器挂接

- `Add` 修正值乘以层数；`Multiply` 按 `1 + (Value - 1) * Stacks` 缩放；`Override` 不缩放。规格中的无效 `StatTag` 或 `NaN` 修正器会被自动过滤。
- 通过 `FGameBuffModifierBridge` 与 `UGameStatComponent::FScopedModifierBatch` / `UpdateStatModifier` 协同：
  - **满层刷新（缩放后修正器完全不变）**：零操作跳过，不触发属性广播。
  - **叠层 / 扣层（属性标签布局不变）**：原地原子更新修正器，仅产生一次增量广播，不回落至基础值，避免依赖生命 Buff 存活的角色在刷新或叠层瞬间误触发死亡。
  - **Spec 变更 / 重绑 / 批量清空**：在批处理作用域内完成替换与回滚，退出时按终态差分统一广播。

### 最小示例

```cpp
FGameBuffSpec Haste;
Haste.BuffTag = FGameplayTag::RequestGameplayTag(TEXT("Buff.Haste"));
Haste.Duration = 10.0f;
Haste.MaxStacks = 3;
Haste.StackPolicy = EGameBuffStackPolicy::RefreshDuration;

FGameStatModifierSpec SpeedMod;
SpeedMod.StatTag = FGameplayTag::RequestGameplayTag(TEXT("Stat.MoveSpeed"));
SpeedMod.Modifier = FGameStatModifier::MakeMultiplier(1.15f);
Haste.Modifiers.Add(SpeedMod);

BuffComponent->ApplyBuff(Haste);
```

## 查询接口

```cpp
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
```

| 函数 | 参数 | 返回 |
| :--- | :--- | :--- |
| `HasBuff` | `InBuffTag` 身份标签 | 存在至少一个活跃实例返回 `true` |
| `HasGrantedTag` | `InTag` 状态标签 | 任一活跃 Buff 的 `GrantedTags` 命中（含父标签层级）返回 `true` |
| `GetBuffStacks` | `InBuffTag` | 该 Tag 全部活跃实例的层数总和；无实例返回 `0` |
| `GetBuffRemainingTime` | `InBuffTag` | 全部实例中的最大剩余秒数；永久 Buff 或无实例返回 `0.0f` |
| `GetActiveBuffTags` | `OutTags` 输出数组 | 去重后的活跃 Buff 身份标签 |
| `GetGrantedTags` | `OutTags` 输出容器 | 全部 Buff 聚合的状态标签集合 |
| `IsAuthoritativeWriteAllowed` | 无 | 允许写入返回 `true` |

- `HasGrantedTag` 直接短路遍历活跃 Buff 的 `GrantedTags`，不构造临时 `FGameplayTagContainer`。
- 查询接口均为 `BlueprintPure` 且不修改状态。

## 事件

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGameBuffChanged, FGameplayTag, BuffTag, int32, Stacks);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameBuffRemoved, FGameplayTag, BuffTag);

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnGameBuffChangedNative, const FGameplayTag&, int32);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnGameBuffRemovedNative, const FGameplayTag&);
```

- `OnBuffChanged` 在层数或状态变化时广播聚合总层数；`OnBuffRemoved` 在该 Tag 完全移除时广播。
- Blueprint 与 C++ 双轨；C++ 侧建议使用 `AddWeakLambda` 绑定拥有者生命周期。

## 权威校验、复制与对象池

- `IsAuthoritativeWriteAllowed()` 与 `UGameStatComponent` 对齐：Owner 为 `nullptr`、Owner 具备权威，或开启 `bAllowNonAuthoritativeWrites` 时允许写入。
- 客户端以 `AcknowledgedStacks` 记录已确认层数，差分基于 `TSortedVectorMap<FGameFastTagKey, int32, 8>`：8 槽内联零堆分配 + 整型快速二分。
- `OnReleaseToPool` 静默清空 `ActiveBuffs`、关闭 Tick，并通过静默回滚通道卸载已挂接修正器（无论与 `UGameStatComponent` 的入池顺序如何均不产生业务广播），同时保持 `NextInstanceId` 单调递增，避免联机延迟包造成实例 ID 碰撞。
- 专属子域日志分类为 `LogGameBuff`：正常流转记录 `Verbose`，非法施加记录 `Warning`，非权威写记录限频 `Warning`。

## 常见错误与边界

- **非法施加**：`BuffTag` 无效或 `Duration` 为 `NaN` / `±Inf` 时返回 `InvalidParameter (11001)`，不会创建实例。
- **非权威写入**：未开启 `bAllowNonAuthoritativeWrites` 时返回 `NotAuthority (11002)`，仅输出限频 `Warning`。
- **独立实例超限**：`Independent` 策略达到 `MaxStacks` 返回 `MaxBuffStacksReached (11006)`；`RefreshDuration` / `AddDuration` 则停在上限并继续刷新 / 累加时长。
- **移除不存在**：`RemoveBuff` / `RemoveBuffStacks` / `RemoveBuffInstance` 未命中返回 `BuffNotFound (11005)`，对应 `K2_*` 返回 `false`。
- **永久 Buff**：`Duration <= 0` 不参与 Tick，`GetBuffRemainingTime` 返回 `0.0f`，只能显式移除。
- **性能口径**：文档中的槽位数量与 O(1) 描述为结构设计常数；README 中的 ns 级基准为历史 Development 构建参考值，未纳入 CI，不构成 Shipping 性能承诺。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [Buff 与冷却指南](../Guides/BuffGuide.md)
- [快速上手](../Guides/QuickStart.md)
- [通用数值组件 API](./GameStatComponent.md)
