# GameGameplayCore 通用数值组件 API 参考

> **一句话**：Tag 驱动注册、修正器派生、按标签订阅与网络复制的通用数值管理组件。
> **头文件**：`Stat/UGameStatComponent.h`、`Core/GameplayCoreErrorCodes.h` · **线程**：写入、查询与订阅回调均限 GameThread
> **失败语义**：C++ 接口返回 `TGameStatusResult<T>` + `Game::GameplayCoreErrorCodes`（`11001~11006`）；查询接口未注册时返回 `0.0f` / `false`

## 30 秒速览

- **注册**：`RegisterStat(Tag, Current, Max, Min)` 注册任意属性并立即广播初始 / 变更后的派生值；`UnregisterStat` 级联清理修正器、订阅者与已确认差分。
- **修正器**：`Add` → `Multiply` → `Override` 顺序演算并与 `Min` 钳制；`AddStatModifier` 返回 64 位强类型代际句柄，`UpdateStatModifier` 原地原子更新。
- **批处理**：`FScopedModifierBatch` 把作用域内多次增删改合并为退出时一次终态差分广播。
- **订阅**：`SubscribeStatChanged` 在属性已注册时仅向本次新订阅者回放当前有效值，内部重入安全。
- **约束**：公开浮点写入口拦截 `NaN` / `±Inf`；非权威端默认拒绝写入，修正器为本地派生层不参与复制。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `UGameStatComponent::RegisterStat` | 注册属性并立即广播派生值 | `Current` 钳制到 `[Min, Max]`；非有限浮点仅记录 `Warning` |
| `UGameStatComponent::UnregisterStat` | 注销属性并级联清理修正器与订阅 | 失败返回 `11001` / `11002` / `11003` |
| `UGameStatComponent::AddStatModifier` | 添加修正器，返回 64 位代际句柄 | 失败返回 `11001` |
| `UGameStatComponent::UpdateStatModifier` | 原地原子更新已有修正器 | 句柄失效返回 `11004` |
| `UGameStatComponent::RemoveStatModifier` | 按句柄移除修正器 | 句柄失效返回 `11004` |
| `UGameStatComponent::ModifyStat` | 增减基础当前值并钳制区间 | 未注册返回 `false` |
| `UGameStatComponent::SetStatCurrent` | 设置当前值 | `void`；钳制到 `[Min, Max]`，非有限值拒绝 |
| `UGameStatComponent::GetStatPercent` | 查询基础值百分比 | 未注册或区间非法返回 `0.0f` |
| `UGameStatComponent::GetStatEffectiveCurrent` | 查询叠加修正器后的派生值 | 未注册返回 `0.0f` |
| `UGameStatComponent::SubscribeStatChanged` | 按属性标签订阅变更 | 仅 GameThread；回放只发给本次新订阅者 |

## 错误码

全部归属 `Game::ModuleId::Gameplay`，由 `Core/GameplayCoreErrorCodes.h` 定义。

| 常量 | 数值 | 触发条件 |
| :--- | :---: | :--- |
| `InvalidParameter` | `11001` | 无效 `GameplayTag`、`NaN` / `±Inf` 数值或非正数层数 |
| `NotAuthority` | `11002` | 非网络权威端且未开启 `bAllowNonAuthoritativeWrites` |
| `StatNotFound` | `11003` | 目标属性标签尚未注册 |
| `ModifierHandleNotFound` | `11004` | 修正器句柄无效、代际过期或不存在 |
| `BuffNotFound` | `11005` | 目标 Buff 标签或运行实例 ID 不存在（由 Buff 组件返回） |
| `MaxBuffStacksReached` | `11006` | 独立实例策略达到最大叠层上限（由 Buff 组件返回） |

## 数据结构

### `FGameStatModifierHandle`

```cpp
struct FStatModifierHandleTag {};
using FGameStatModifierHandle = TGameHandle<FStatModifierHandleTag>;
```

- 64 位强类型代际句柄（32 位序号 + 32 位代际号），防止与其他模块句柄混用；对象池重置后历史句柄自动失效，避免 ABA 误删。

### `FGameStatValue`

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

| 成员 | 说明 |
| :--- | :--- |
| `Current` | 基础当前值 |
| `Max` | 基础上限 |
| `Min` | 基础下限 |
| `EvaluatePercent` | 按 `[Min, Max]` 归一化并钳制到 `[0, 1]`；区间非法返回 `0.0f` |
| `GetPercent` | 当前值百分比（0~1） |
| `operator==` | 三项均浮点近似相等时返回 `true` |

### `EGameStatModifierOp` 与 `FGameStatModifier`

```cpp
UENUM(BlueprintType)
enum class EGameStatModifierOp : uint8 {
    Add,
    Multiply,
    Override
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

| 枚举值 | 演算 |
| :--- | :--- |
| `Add` | 加算 |
| `Multiply` | 乘算 |
| `Override` | 覆写，按注册顺序后注册者覆盖先注册者 |

- 修正器按注册顺序演算：**先全部加算 → 再全部乘算 → 最后覆写**，最终与 `Min` 做 `FMath::Max` 下限钳制。
- `bApplyToCurrent` / `bApplyToMax` 控制修正作用于当前值 / 上限；`MakeMultiplier` 为乘算便捷构造。

### 委托

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

## 注册与查询

```cpp
UFUNCTION(BlueprintCallable, Category = "Game|Stat")
void RegisterStat(const FGameplayTag& InStatTag, float InInitialCurrent, float InMax = 100.0f, float InMin = 0.0f);

UFUNCTION(BlueprintCallable, Category = "Game|Stat", meta = (DisplayName = "Unregister Stat"))
bool K2_UnregisterStat(const FGameplayTag& InStatTag);
TGameStatusResult<void> UnregisterStat(const FGameplayTag& InStatTag);

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
```

| 函数 | 参数 | 返回 | 失败 |
| :--- | :--- | :--- | :--- |
| `RegisterStat` | 标签、初始当前值、上限、下限 | `void` | 无效标签或非有限浮点被拒绝并记录 `Warning` |
| `UnregisterStat` | `InStatTag` | `Success` | `11001` / `11002` / `11003` |
| `HasStat` | `InStatTag` | 已注册返回 `true` | — |
| `GetAllStatTags` | `OutTags` | 全部已注册标签 | — |
| `GetStatCurrent` / `GetStatMax` / `GetStatMin` | `InStatTag` | 基础值；未注册返回 `0.0f` | — |
| `GetStatPercent` | `InStatTag` | `[0, 1]` 基础百分比；未注册或区间非法返回 `0.0f` | — |
| `GetStatValue` | `InStatTag` | 完整 `FGameStatValue` | `11003` |

- `RegisterStat` 会钳制 `Current` 到 `[Min, Max]`，并把 `Max` 抬升至不小于 `Min`，随后立即广播派生值。
- `UnregisterStat` 使用 `RemoveAtSwap` + O(1) 索引同步移除，并级联清理修正器、订阅者与已确认差分。
- 非权威端写入默认拒绝，日志分类 `LogGameStat`，输出限频 `Warning`。

### 最小示例

```cpp
const FGameplayTag HealthTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Health"));
StatComponent->RegisterStat(HealthTag, 100.0f, 100.0f, 0.0f);

const bool bRegistered = StatComponent->HasStat(HealthTag);
const float HealthPercent = StatComponent->GetStatPercent(HealthTag);
```

## 修正器与派生值

```cpp
class GAMEGAMEPLAYCORE_API FScopedModifierBatch;

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
```

| 函数 | 参数 | 返回 | 失败 |
| :--- | :--- | :--- | :--- |
| `AddStatModifier` | 标签、修正器 | 强类型代际句柄 | `11001` |
| `UpdateStatModifier` | 标签、句柄、新修正器 | `Success`，原地原子更新 | `11001` / `11004` |
| `RemoveStatModifier` | 标签、句柄 | `Success` | `11004` |
| `ClearStatModifiers` | 标签 | 被清除的修正器数量 | — |
| `GetStatEffectiveCurrent` / `Max` / `Percent` | 标签 | 叠加全部修正器后的派生值；未注册返回 `0.0f` | — |

- 修正器属于本地派生层，不直接网络复制；客户端通过 Buff / 装备同步各自重建。
- `FScopedModifierBatch` 为 RAII 守卫，支持嵌套；最外层退出时按终态差分统一广播，避免中间态回落抖动。
- 蓝图门面以打包 `int64` 句柄交互，`0` 表示失败。

### 最小示例

```cpp
{
    UGameStatComponent::FScopedModifierBatch Batch(*StatComponent);

    const TGameStatusResult<FGameStatModifierHandle> AddResult =
        StatComponent->AddStatModifier(HealthTag, FGameStatModifier(50.0f, EGameStatModifierOp::Add));
    if (AddResult.IsSuccess()) {
        StatComponent->UpdateStatModifier(
            HealthTag,
            AddResult.GetValue(),
            FGameStatModifier::MakeMultiplier(1.2f)
        );
    }
}

const float EffectiveMax = StatComponent->GetStatEffectiveMax(HealthTag);
```

## 统一修改接口

```cpp
UFUNCTION(BlueprintCallable, Category = "Game|Stat")
bool ModifyStat(const FGameplayTag& InStatTag, float Delta, float& OutActualDelta);
bool ModifyStat(const FGameplayTag& InStatTag, float Delta);

UFUNCTION(BlueprintCallable, Category = "Game|Stat")
void SetStatCurrent(const FGameplayTag& InStatTag, float NewCurrent);

UFUNCTION(BlueprintCallable, Category = "Game|Stat")
void SetStatMax(const FGameplayTag& InStatTag, float NewMax, bool bKeepRelativePercent = true);
```

| 函数 | 参数 | 返回 / 行为 |
| :--- | :--- | :--- |
| `ModifyStat` | 标签、增量、`OutActualDelta` | 基础值实际变动返回 `true`；结果钳制到 `[Min, Max]` |
| `ModifyStat`（重载） | 标签、增量 | 忽略实际增量 |
| `SetStatCurrent` | 标签、新当前值 | `void`；钳制到 `[Min, Max]` |
| `SetStatMax` | 标签、新上限、是否保持百分比 | `void`；`Max` 抬升至不小于 `Min`，按需同比缩放当前值 |

- 全部公开浮点写入口在边界拦截 `NaN` / `±Inf`，防止属性毒化或错误满血。
- 仅当派生有效值实际变化时才广播 `OnStatChanged` / `OnStatChangedNative`。

## 按 Tag 订阅

```cpp
FDelegateHandle SubscribeStatChanged(const FGameplayTag& InStatTag, FOnGameStatChangedNative::FDelegate InDelegate);
FDelegateHandle SubscribeStatChanged(UObject* Owner, const FGameplayTag& InStatTag,
    TFunction<void(const FGameplayTag&, float, float, float)> InCallback);
void UnsubscribeStatChanged(const FGameplayTag& InStatTag, FDelegateHandle InHandle);
```

| 重载 | 参数 | 返回 |
| :--- | :--- | :--- |
| 委托版 | 标签、`FOnGameStatChangedNative::FDelegate` | 订阅句柄 |
| 回调版 | `Owner` 弱生命周期归属、标签、回调 | 订阅句柄；`Owner` 非空且已失效时拒绝订阅 |

- 两种重载在属性已注册时**仅向本次新订阅者**立即回放当前有效值，既有订阅者不会重复收到通知。
- 内部 `FGameStatSubscriptionHub` 采用 `TSharedRef` 广播期保活 + `CreateWeakLambda` 弱绑定，免疫回调内自退订、订阅表重排与 Owner 中途销毁。
- 回调参数为 `(StatTag, OldValue, NewValue, MaxValue)`，均为派生有效值。

### 最小示例

```cpp
HealthSubscription = StatComponent->SubscribeStatChanged(
    this,
    HealthTag,
    [this](const FGameplayTag& Tag, float OldValue, float NewValue, float MaxValue) {
        UpdateHealthBar(NewValue, MaxValue);
    }
);
```

## 权威校验与对象池

- `IsAuthoritativeWriteAllowed()`：Owner 为 `nullptr`、Owner 具备权威，或开启 `bAllowNonAuthoritativeWrites` 时允许写入。
- `OnReleaseToPool` 静默清空修正器（并递增代际号使历史句柄全部失效）、把全部已注册属性回满至 `Max`、对齐 `AcknowledgedDiff`，不向外广播业务事件，避免死亡实体入池误触发复活事件。
- 专属日志分类 `LogGameStat`：正常流转 `Verbose`，非法写入与非权威拒绝 `Warning`（限频）。

## 常见错误与边界

- **非有限浮点**：注册、修改、设值、修正器写入均拒绝 `NaN` / `±Inf`；其中 `RegisterStat` / `SetStatCurrent` / `SetStatMax` 为 `void`，失败仅记录 `Warning` 不返回错误码。
- **非权威写入**：默认拒绝基础值修改并返回 `NotAuthority (11002)`（`void` 接口仅记录限频 `Warning`）。
- **句柄失效**：对象池归还或属性注销后，历史 `FGameStatModifierHandle` 不再命中，更新 / 移除返回 `ModifierHandleNotFound (11004)`。
- **未注册属性**：查询返回 `0.0f` / `false`，`GetStatValue` 返回 `StatNotFound (11003)`，`ModifyStat` 返回 `false`。
- **订阅回放范围**：回放只发给本次新订阅者，不要依赖订阅事件去同步其他既有监听者。
- **性能口径**：文档中的 O(1) 与槽位数量为结构设计复杂度；README 基准表中的 ns 级数字为历史 Development 构建参考值，未纳入 CI，不构成 Shipping 性能承诺。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [生命与存活状态组件 API](./GameVitalComponent.md)
- [通用 Buff 组件 API](./BuffComponent.md)
- [数值组件实战指南](../Guides/StandaloneStatGuide.md)
- [快速上手](../Guides/QuickStart.md)
