# GameGameplay GAS Buff 桥接 API 参考

> **一句话**：`UGameGasBuffBridge` 是以 `GameplayEffect` 为载体的 Buff 统一入口，收敛上下文构建、同体/异体施加、移除与只读查询。
> **头文件**：`#include "Bridge/UGameGasBuffBridge.h"` · **线程**：施加、移除与查询均随 ASC 操作在 GameThread 执行 · **失败语义**：施加失败返回无效 `FActiveGameplayEffectHandle`，移除失败返回 `false`，查询失败返回 `false` / `0` / `0.0f`

## 30 秒速览

- **职责边界**：时长、叠层、标签与属性修改全部由 `GameplayEffect` 原生完成，桥接层不引入具体 Buff 类型或业务数值。
- **同体施加**：`InSourceASC` 与目标 ASC 相同时调用 `ApplyGameplayEffectSpecToSelf`。
- **异体施加**：`InSourceASC` 与目标 ASC 不同时调用 `ApplyGameplayEffectSpecToTarget`，保证施法者属性快照与击杀归属准确。
- **省略施法者**：`InSourceASC == nullptr` 时由目标 ASC 自施加；显式传入但已失效的施法者会被拒绝，不会回退。
- **非 GAS 项目**：改用 `GameGameplayCore` 的 `UGameBuffComponent`，不要走本桥接。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `UGameGasBuffBridge::ApplyBuffEffect` | 以 GE 类对目标施加 Buff | GameThread；失败返回无效 `FActiveGameplayEffectHandle` |
| `UGameGasBuffBridge::RemoveBuffEffect` | 按句柄移除 Buff 或指定层数 | ASC / 句柄无效返回 `false`，`-1` 为全部移除 |
| `UGameGasBuffBridge::IsBuffEffectActive` | 查询 Buff 是否仍处于激活态 | 无效 ASC / 句柄返回 `false` |
| `UGameGasBuffBridge::GetBuffStackCount` | 查询当前叠层数 | 无效或未激活返回 `0` |
| `UGameGasBuffBridge::GetBuffRemainingTime` | 查询剩余时长 | 无限时长返回 `-1.0f`；有限时长无 `World` 返回 `0.0f` |
| `UGameGasBuffBridge::GetBuffDuration` | 查询 Buff 配置总时长 | 无限时长返回 `-1.0f`；不影响 ASC 状态 |

## 施加与移除

```cpp
UCLASS()
class GAMEGAMEPLAY_API UGameGasBuffBridge : public UBlueprintFunctionLibrary {
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    static FActiveGameplayEffectHandle ApplyBuffEffect(
        UAbilitySystemComponent* InAbilitySystemComponent,
        TSubclassOf<UGameplayEffect> InBuffEffectClass,
        int32 InLevel = 1,
        UObject* InSourceObject = nullptr,
        UAbilitySystemComponent* InSourceASC = nullptr
    );

    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    static bool RemoveBuffEffect(
        UAbilitySystemComponent* InAbilitySystemComponent,
        FActiveGameplayEffectHandle InHandle,
        int32 StacksToRemove = -1
    );
};
```

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `ApplyBuffEffect` | 目标 ASC、GE 类、等级、来源对象、施法者 ASC | `FActiveGameplayEffectHandle` | 失败返回无效句柄 |
| `RemoveBuffEffect` | 目标 ASC、激活句柄、移除层数（`-1` 为全部） | `bool` | ASC 或句柄无效返回 `false` |

- **线程**：`MakeEffectContext`、`MakeOutgoingSpec` 与 Apply 系列均须在 GameThread 调用。
- **生命周期**：返回的句柄由调用方保存，用于后续移除与查询；效果本身随 GE 时长、叠层与 ASC 生命周期管理。
- **失败语义**：目标 ASC 无效、GE 类为空、目标或施法者未完成 `InitAbilityActorInfo`、目标缺少有效 `World`、Spec 构建失败时，打印 Warning 并返回无效句柄。
- **失效施法者**：显式传入非空但已失效（PendingKill / Garbage）的 `InSourceASC` 时直接拒绝，不会静默回退为目标自施加。

## 只读查询

```cpp
UFUNCTION(BlueprintPure, Category = "Game|Buff")
[[nodiscard]] static bool IsBuffEffectActive(
    const UAbilitySystemComponent* InAbilitySystemComponent,
    FActiveGameplayEffectHandle InHandle
);

UFUNCTION(BlueprintPure, Category = "Game|Buff")
[[nodiscard]] static int32 GetBuffStackCount(
    const UAbilitySystemComponent* InAbilitySystemComponent,
    FActiveGameplayEffectHandle InHandle
);

UFUNCTION(BlueprintPure, Category = "Game|Buff")
[[nodiscard]] static float GetBuffRemainingTime(
    const UAbilitySystemComponent* InAbilitySystemComponent,
    FActiveGameplayEffectHandle InHandle
);

UFUNCTION(BlueprintPure, Category = "Game|Buff")
[[nodiscard]] static float GetBuffDuration(
    const UAbilitySystemComponent* InAbilitySystemComponent,
    FActiveGameplayEffectHandle InHandle
);
```

| 方法 | 返回 | 无效 ASC / 句柄 | 未激活 |
| :--- | :--- | :--- | :--- |
| `IsBuffEffectActive` | `bool` | `false` | `false` |
| `GetBuffStackCount` | `int32` | `0` | `0` |
| `GetBuffRemainingTime` | `float` | `0.0f` | `0.0f` |
| `GetBuffDuration` | `float` | `0.0f` | `0.0f` |

- **线程**：四个函数全部接受 `const UAbilitySystemComponent*`，可在只读上下文安全调用。
- **无限时长**：`GetBuffRemainingTime` 与 `GetBuffDuration` 均返回 `-1.0f`，与是否有 `World` 无关。
- **有限时长无 `World`**：`GetBuffRemainingTime` 返回 `0.0f`，`GetBuffDuration` 仍返回配置的总时长。
- **失败语义**：仅返回默认值，不修改 ASC 状态、不打印错误。

## 最小示例

```cpp
#include "Bridge/UGameGasBuffBridge.h"

void UMyBuffService::ApplyHaste(
    UAbilitySystemComponent* TargetASC,
    UAbilitySystemComponent* CasterASC,
    TSubclassOf<UGameplayEffect> HasteEffect
) {
    ActiveHasteHandle = UGameGasBuffBridge::ApplyBuffEffect(TargetASC, HasteEffect, 1, this, CasterASC);
}

void UMyBuffService::ClearHaste(UAbilitySystemComponent* TargetASC) {
    if (ActiveHasteHandle.IsValid()) {
        UGameGasBuffBridge::RemoveBuffEffect(TargetASC, ActiveHasteHandle);
        ActiveHasteHandle.Invalidate();
    }
}
```

## 常见错误与边界

- **失效施法者必须显式置空**：传入已失效的非空 `InSourceASC` 会拒绝施加；若确实要目标自施加，请传 `nullptr`。
- **初始化时序**：目标与施法者 ASC 必须先完成 `InitAbilityActorInfo`，目标还须有有效 `World`，否则一律拒绝。
- **同体与异体**：同体走 `ApplyGameplayEffectSpecToSelf`，异体走 `ApplyGameplayEffectSpecToTarget`；不要自行绕过桥接调用，以免丢失来源快照。
- **无限时长不是 0**：查询无限时长 Buff 时返回 `-1.0f`，用 `<= 0` 判断“已结束”会把无限 Buff 误判为过期。
- **句柄失效**：持有一个已移除效果的旧句柄调用查询只会得到默认值，不区分“已过期”和“从未存在”。
- **职责越界**：桥接不定义 Buff 数值、叠层规则或标签，这些必须由 `GameplayEffect` 资产承载；非 GAS 项目请使用 `UGameBuffComponent`。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [快速上手](../Guides/QuickStart.md)
