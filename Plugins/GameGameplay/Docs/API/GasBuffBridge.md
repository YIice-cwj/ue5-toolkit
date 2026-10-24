# GameGameplay GAS Buff 桥接 API 参考

`UGameGasBuffBridge` 是 GAS 项目中承载 Buff 的统一入口：时长、叠层、标签与属性修改仍由 `GameplayEffect` 原生完成，桥接层只收敛「施加 / 移除 / 查询」样板代码，不引入任何具体 Buff 类型或业务数值。

- 头文件：`#include "AbilitySystem/UGameGasBuffBridge.h"`
- 形态：纯静态函数，无状态
- 适用：已接入 GAS 的项目；非 GAS 项目使用 `GameGameplayCore` 的 `UGameBuffComponent`

---

## 1. 接口

```cpp
UCLASS()
class GAMEGAMEPLAY_API UGameGasBuffBridge : public UObject {
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Game|Buff", meta = (DefaultToSelf = "InAbilitySystemComponent"))
    static FActiveGameplayEffectHandle ApplyBuffEffect(
        UAbilitySystemComponent* InAbilitySystemComponent,
        TSubclassOf<UGameplayEffect> InBuffEffectClass,
        int32 InLevel = 1,
        UObject* InSourceObject = nullptr
    );

    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    static bool RemoveBuffEffect(
        UAbilitySystemComponent* InAbilitySystemComponent,
        FActiveGameplayEffectHandle InHandle);

    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    static bool IsBuffEffectActive(
        UAbilitySystemComponent* InAbilitySystemComponent,
        FActiveGameplayEffectHandle InHandle);

    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    static int32 GetBuffStackCount(
        UAbilitySystemComponent* InAbilitySystemComponent,
        FActiveGameplayEffectHandle InHandle);

    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    static float GetBuffRemainingTime(
        UAbilitySystemComponent* InAbilitySystemComponent,
        FActiveGameplayEffectHandle InHandle);

    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    static float GetBuffDuration(
        UAbilitySystemComponent* InAbilitySystemComponent,
        FActiveGameplayEffectHandle InHandle);
};
```

---

## 2. 语义

- `ApplyBuffEffect`：构建效果上下文并施加；失败返回无效句柄。
- `RemoveBuffEffect`：按激活句柄移除；ASC 或句柄无效返回 `false`。
- `IsBuffEffectActive`：查询效果是否仍激活，供 UI 与技能条件使用。
- `GetBuffStackCount`：查询该 Buff 实例当前生效的堆叠层数，未命中返回 0。
- `GetBuffRemainingTime`：根据当前 World 时钟精确查询剩余秒数，用于 UI 倒计时展示。
- `GetBuffDuration`：查询该 Buff 实例的总持续时长（秒）。
- 桥接层不做权威判断，网络权限由 `GameplayEffect` 自身配置与 ASC 权威决定。

---

## 3. 示例

```cpp
#include "AbilitySystem/UGameGasBuffBridge.h"

void UMyBuffService::ApplyHaste(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayEffect> HasteEffect)
{
    if (!ASC) return;

    ActiveHasteHandle = UGameGasBuffBridge::ApplyBuffEffect(ASC, HasteEffect, 1, this);
}

void UMyBuffService::ClearHaste(UAbilitySystemComponent* ASC)
{
    if (!ASC || !ActiveHasteHandle.IsValid()) return;

    UGameGasBuffBridge::RemoveBuffEffect(ASC, ActiveHasteHandle);
    ActiveHasteHandle.Invalidate();
}
```

---

## 4. 选型

| 项目形态 | 推荐 |
| :--- | :--- |
| 已接入 GAS | `UGameGasBuffBridge` + `GameplayEffect` |
| 非 GAS / 轻量实体 | `UGameBuffComponent`（GameGameplayCore） |

两边共享同一原则：框架只提供运行时契约，具体 Buff 由开发者用 `GameplayTag` 与数据资产自由定义。
