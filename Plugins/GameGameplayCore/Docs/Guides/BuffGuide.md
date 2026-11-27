# GameGameplayCore Buff 与冷却指南

本文演示如何用 `UGameBuffComponent` 注册任意 Buff，并把冷却表达为「带持续时长与状态标签的 Buff」。

---

## 1. 在 Actor 上挂载组件

```cpp
#include "Buff/UGameBuffComponent.h"
#include "Stat/UGameStatComponent.h"

ACombatCharacter::ACombatCharacter()
{
    StatComponent = CreateDefaultSubobject<UGameStatComponent>(TEXT("StatComponent"));
    BuffComponent = CreateDefaultSubobject<UGameBuffComponent>(TEXT("BuffComponent"));
}
```

- `UGameBuffComponent` 默认从 Owner 自动查找 `UGameStatComponent`；组件不在同一 Actor 时调用 `BindStatComponent` 显式绑定。
- 组件默认开启复制且要求服务端权威（`bAllowNonAuthoritativeWrites = false`）。

---

## 2. 定义任意 Buff

```cpp
FGameBuffSpec MakePoisonSpec()
{
    FGameBuffSpec Spec;
    Spec.BuffTag = FGameplayTag::RequestGameplayTag(TEXT("Buff.Debuff.Poison"));
    Spec.GrantedTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("State.Poisoned")));
    Spec.Duration = 6.0f;
    Spec.MaxStacks = 5;
    Spec.StackPolicy = EGameBuffStackPolicy::AddDuration;

    FGameStatModifierSpec Slow;
    Slow.StatTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.MoveSpeed"));
    Slow.Modifier = FGameStatModifier(-20.0f, EGameStatModifierOp::Add);
    Spec.Modifiers.Add(Slow);

    return Spec;
}
```

- 框架不预置任何 Buff 类型，Tag、时长、层数、修正器全部由业务决定。
- 激活期间可用零拷贝 `HasGrantedTag(StateTag)` 快速判断状态（如眩晕、中毒、冷却中），或通过 `GetGrantedTags(OutTags)` 导出完整标签集。

---

## 3. 施加、叠层与移除

```cpp
const TGameStatusResult<int32> ApplyRes = BuffComponent->ApplyBuff(MakePoisonSpec());
const int32 InstanceId = ApplyRes.IsSuccess() ? ApplyRes.GetValue() : 0;

const bool bPoisoned = BuffComponent->HasBuff(PoisonTag);
const bool bHasState = BuffComponent->HasGrantedTag(FGameplayTag::RequestGameplayTag(TEXT("State.Poisoned")));
const int32 Stacks = BuffComponent->GetBuffStacks(PoisonTag);
const float Remaining = BuffComponent->GetBuffRemainingTime(PoisonTag);

// 按层削减 Buff（如驱散或掉层）
BuffComponent->RemoveBuffStacks(PoisonTag, 2);

// 整项移除或按实例精确移除
BuffComponent->RemoveBuff(PoisonTag, true);
BuffComponent->RemoveBuffInstance(InstanceId);

// 角色死亡或大驱散时一键清空全部 Buff
BuffComponent->ClearAllBuffs();
```

- `RefreshDuration`：重复施加只刷新时长并加层；满层刷新时若修正器未变则零重建、零属性广播抖动。
- `AddDuration`：重复施加累加时长并加层。
- `Independent`：每次施加独立实例，适合可并存的多段 DOT（受 `MaxStacks` 限制，超限时返回 `Game::GameplayCoreErrorCodes::MaxBuffStacksReached`）。

---

## 4. 冷却即 Buff

冷却不需要独立类，只需一个限时 Buff：

```cpp
FGameBuffSpec MakeCooldownSpec()
{
    FGameBuffSpec Spec;
    Spec.BuffTag = FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Skill.Fireball"));
    Spec.GrantedTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("State.Cooldown")));
    Spec.Duration = 8.0f;
    Spec.MaxStacks = 1;
    Spec.StackPolicy = EGameBuffStackPolicy::RefreshDuration;
    return Spec;
}
```

- `HasBuff(CooldownTag)` 或 `HasGrantedTag(StateCooldownTag)` 即「冷却中」。
- `GetBuffRemainingTime(CooldownTag)` 直接驱动 UI 倒计时。
- 冷却结束由 Tick 自动移除并广播 `OnBuffRemoved`，无需业务侧计时器。

---

## 5. UI 绑定

```cpp
BuffComponent->OnBuffChangedNative.AddWeakLambda(this,
    [this](const FGameplayTag& BuffTag, int32 Stacks) {
        RefreshBuffIcon(BuffTag, Stacks);
    });

BuffComponent->OnBuffRemovedNative.AddWeakLambda(this,
    [this](const FGameplayTag& BuffTag) {
        RemoveBuffIcon(BuffTag);
    });
```

- 客户端由 FastArray 复制回调（`PostReplicatedAdd` / `PostReplicatedChange` / `PreReplicatedRemove`）差分触发同一组事件，移除最后一条限时 Buff 时当帧关闭 Tick。
- 监听者销毁时弱 Lambda 自动失效，无需手动解绑。

---

## 6. 与 GAS 的分工

| 项目形态 | 推荐 |
| :--- | :--- |
| 非 GAS / 轻量实体 | `UGameBuffComponent` |
| 已接入 GAS | `UGameGasBuffBridge` + `GameplayEffect`（见 GameGameplay 文档） |

两种方式都遵守同一原则：框架不定义具体 Buff，只提供注册与运行时。
