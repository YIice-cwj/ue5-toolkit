# GameGameplay GAS Buff 桥接 API 参考

`UGameGasBuffBridge` 是 GAS 项目中承载 Buff 的统一入口：时长、叠层、标签与属性修改仍由 `GameplayEffect` 原生完成，桥接层收敛“上下文构建 + 同体/异体施加 + 移除 + 只读查询”样板代码，不引入任何具体 Buff 类型或业务数值。

- 头文件：`#include "Bridge/UGameGasBuffBridge.h"`
- 形态：纯静态函数库，无状态
- 适用：已接入 GAS 的项目；非 GAS 项目使用 `GameGameplayCore` 的 `UGameBuffComponent`

---

## 1. 接口

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
};
```

---

## 2. 语义

- `ApplyBuffEffect`：若传入有效的 `InSourceASC` 则以施法者 ASC 构建上下文并调用 `ApplyGameplayEffectSpecToTarget` 施加给目标（保证施法者属性快照与击杀来源归属准确）；`InSourceASC == nullptr` 时回退由目标 ASC 自施加；若显式传入非空但已失效（PendingKill/Garbage）的 `InSourceASC`，或目标/施法者 ASC 尚未完成 `InitAbilityActorInfo` 或目标缺少有效 `World`，则打印告警并安全拒绝施加（避免底层 `MakeEffectContext` / `GetWorldTime` 崩溃）。
- `RemoveBuffEffect`：按激活句柄移除指定层数（默认 `-1` 移除全部层数）；ASC 或句柄无效返回 `false`。
- `IsBuffEffectActive` / `GetBuffStackCount` / `GetBuffRemainingTime` / `GetBuffDuration`：全部接受 `const UAbilitySystemComponent*`，可在只读上下文安全调用。
- `GetBuffRemainingTime`：返回剩余秒数；**无限时长 Buff 恒返回 `-1.0f`**（即使无 World 亦与 `GetBuffDuration` 保持一致），未激活或有限时长且无 World 返回 `0.0f`。
- `GetBuffDuration`：返回总持续时长（秒）；无限时长返回 `-1.0f`，未激活返回 `0.0f`。

---

## 3. 示例

```cpp
#include "Bridge/UGameGasBuffBridge.h"

void UMyBuffService::ApplyHaste(UAbilitySystemComponent* TargetASC, UAbilitySystemComponent* CasterASC, TSubclassOf<UGameplayEffect> HasteEffect) {
    ActiveHasteHandle = UGameGasBuffBridge::ApplyBuffEffect(TargetASC, HasteEffect, 1, this, CasterASC);
}

void UMyBuffService::ClearHaste(UAbilitySystemComponent* TargetASC) {
    if (ActiveHasteHandle.IsValid()) {
        UGameGasBuffBridge::RemoveBuffEffect(TargetASC, ActiveHasteHandle);
        ActiveHasteHandle.Invalidate();
    }
}
```
