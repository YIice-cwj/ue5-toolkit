# GameGameplay 通用能力基类 API 参考

> **一句话**：`UGameGameplayAbility` 是全项目 GameplayAbility 的抽象父类，固化状态标签激活守卫、触发来源记账与结束自动复位。
> **头文件**：`#include "Ability/UGameGameplayAbility.h"` · **线程**：`CanActivateAbility` / `ActivateAbility` / `EndAbility` 随 GAS 激活流程在 GameThread 执行 · **失败语义**：命中 `BlockingStateTags` 时 `CanActivateAbility` 返回 `false`，不改变能力状态

## 30 秒速览

- **抽象基类**：`UCLASS(Abstract)`，仅提供守卫与记账，具体能力必须由游戏层派生。
- **实例策略**：构造函数固定 `InstancedPerActor + ReplicateNo`，消除全部能力的 Subobject 复制通道开销，网络同步沿用 GAS 原生预测管线。
- **守卫**：`CanActivateAbility` 先走 `Super`，再检查 `BlockingStateTags`，命中时把具体标签写入 `OptionalRelevantTags` 并返回 `false`。
- **来源记账**：`ActivateAbility` 解析并记录 `EGameAbilityTriggerSource`，`EndAbility` 在能力确认非活跃后复位为 `Input`。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `UGameGameplayAbility::CanActivateAbility` | 激活守卫，先走 `Super` 再检查状态标签 | 命中 `BlockingStateTags` 返回 `false`，不改变能力状态 |
| `UGameGameplayAbility::ActivateAbility` | 解析记录触发来源后转交 `Super` 激活 | 仅 GameThread；子类覆写必须调用 `Super` |
| `UGameGameplayAbility::EndAbility` | 结束能力并在确认非活跃后复位来源 | `Super` 回调后复位为 `Input`；需调用 `Super` |
| `UGameGameplayAbility::GetTriggerSource` | 读取本次激活的触发来源 | 仅在当前激活期间有效 |
| `UGameGameplayAbility::BlockingStateTags` | 配置短路激活的状态标签集 | 未配置则守卫完全放行，不阻断激活 |
| `UGameGameplayAbility::TriggerSourceTagMap` | 配置事件标签到触发来源的映射 | 未命中的非空事件统一回落为 `Effect` |

## 触发来源 `EGameAbilityTriggerSource`

```cpp
UENUM(BlueprintType)
enum class EGameAbilityTriggerSource : uint8 {
    Input,
    Effect,
    AI,
};
```

| 枚举值 | 说明 |
| :--- | :--- |
| `Input` | 玩家按键或手柄输入触发，也是复位后的默认值 |
| `Effect` | 外部 GameplayEffect 或事件标签被动触发 |
| `AI` | 行为树或 AI 控制器调用触发 |

- **线程**：普通 `uint8` 枚举值，可跨线程拷贝，无共享状态。
- **生命周期**：`TriggerSource` 为实例成员，仅在当前激活期间有效。
- **失败语义**：解析失败不报错，按规则回落（见下表）。

| `TriggerEventData` 情况 | 解析结果 |
| :--- | :--- |
| 为空指针 | `Input` |
| `EventTag` 命中 `TriggerSourceTagMap` | 映射配置值 |
| `EventTag` 未命中 | `Effect` |

### 最小示例

```cpp
if (MyAbility->GetTriggerSource() == EGameAbilityTriggerSource::AI) {
    PlayAiActivationCue();
}
```

## `UGameGameplayAbility`

```cpp
UCLASS(Abstract)
class GAMEGAMEPLAY_API UGameGameplayAbility : public UGameplayAbility {
    GENERATED_BODY()

public:
    UGameGameplayAbility();

    bool CanActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags = nullptr,
        const FGameplayTagContainer* TargetTags = nullptr,
        OUT FGameplayTagContainer* OptionalRelevantTags = nullptr
    ) const override;

    void ActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData
    ) override;

    void EndAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        bool bReplicateEndAbility,
        bool bWasCancelled
    ) override;

    [[nodiscard]] FORCEINLINE EGameAbilityTriggerSource GetTriggerSource() const noexcept;
};
```

| 方法 | 参数 | 返回 | 行为 |
| :--- | :--- | :--- | :--- |
| `CanActivateAbility` | 规格句柄、宿主信息、来源标签、目标标签、可选相关标签输出 | `bool` | `Super` 通过后检查 `BlockingStateTags`；命中时逐项写入命中标签并返回 `false` |
| `ActivateAbility` | 规格句柄、宿主信息、激活信息、触发事件数据 | `void` | 先解析记录触发来源，再转交 `Super` |
| `EndAbility` | 规格句柄、宿主信息、激活信息、是否复制结束、是否被取消 | `void` | 先转交 `Super`，确认 `!IsActive()` 后把触发来源复位为 `Input` |
| `GetTriggerSource` | — | `EGameAbilityTriggerSource` | 返回本次激活的触发来源 |

| 配置属性 | 类型 | 说明 |
| :--- | :--- | :--- |
| `BlockingStateTags` | `FGameplayTagContainer` | 持有任一标签即短路激活，具体标签由游戏层配置 |
| `TriggerSourceTagMap` | `TMap<FGameplayTag, EGameAbilityTriggerSource>` | 事件标签到触发来源的映射 |

- **线程**：GAS 激活、结束与守卫判定均发生在 GameThread。
- **生命周期**：`InstancedPerActor` 实例跨多次激活复用，`EndAbility` 负责清除本次触发来源残留。
- **失败语义**：`BlockingStateTags` 为空或 `ActorInfo` / ASC 为空时不阻断，返回 `Super` 的结果；守卫不修改能力状态。

### 最小示例

```cpp
UCLASS()
class UMyDashAbility : public UGameGameplayAbility {
    GENERATED_BODY()

public:
    void ActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData
    ) override {
        Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
        if (!CommitAbility(Handle, ActorInfo, ActivationInfo)) {
            EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        }
    }
};
```

## 常见错误与边界

- **直接实例化**：基类为 `Abstract`，不能生成可用的能力实例；测试若需实例化，必须清除 `CLASS_Abstract` 并在用后归还。
- **遗漏 `Super`**：子类覆写 `CanActivateAbility` / `ActivateAbility` / `EndAbility` 时必须调用 `Super`，否则守卫、记账与复位全部失效。
- **复位时机**：`EndAbility` 先调用 `Super`，因此结束回调内仍可读取本次触发来源；回调结束后才复位为 `Input`。
- **阻断标签为空**：`BlockingStateTags` 未配置时守卫完全放行，死亡/硬直拦截需要游戏层显式配置标签。
- **事件标签回落**：`TriggerSourceTagMap` 未命中且 `TriggerEventData` 非空时统一记为 `Effect`，不会记为 `AI`。
- **网络复制预期**：默认 `ReplicateNo`，不要依赖能力实例自身复制；同步应沿用 GAS 原生预测与 GE 管线。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [启动与生命周期](../Architecture/BootstrapAndLifecycle.md)
- [快速上手](../Guides/QuickStart.md)
