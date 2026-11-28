# GameGameplay GAS GameplayCue 表现桥接 API 参考

> **一句话**：`UGameplayCueNotify_GameFeedback` 把 GAS 原生 `GameplayCue` 事件转换为 `FGameFeedbackSignal`，并派发到 GameFeedback 表现总线。
> **头文件**：`#include "Bridge/UGameplayCueNotify_GameFeedback.h"`、`#include "Bridge/UGameFeedbackAdapter.h"` · **线程**：`OnExecute_Implementation` 随 Cue 执行在 GameThread 调度，处理器无状态共享 CDO · **失败语义**：`MyTarget` 为空返回 `false` 且不广播，其余返回 `true`

## 30 秒速览

- **形态**：继承 `UGameplayCueNotify_Static` 的静态无状态桥接器，不持有实例数据。
- **强化链路**：当 `Parameters.EffectContext` 可提取为 `FGameGameplayEffectContext` 时，用 `FGameDamageResult` 还原完整信号。
- **主通道来源**：强化链路的主通道必须取伤害结果中的主伤害通道 `GetDominantActualChannel()`，不能使用 Cue 的 `MatchedTagName`。
- **原生回退**：无法提取扩展上下文时，用 `RawMagnitude`、`Location` / `Normal` 组装基础信号；回退分支不设置 `PrimaryChannel`，禁止把 Cue 的 `MatchedTagName` 当作伤害通道。
- **出口**：统一调用 `UGameFeedbackBlueprintLibrary::BroadcastFeedback` 投递到 `Feedback.Channel.Play`。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `UGameplayCueNotify_GameFeedback::OnExecute_Implementation` | 把 Cue 事件转换为反馈信号并广播 | GameThread；`MyTarget` 为空返回 `false` 且不广播 |
| `FGameGameplayEffectContext::ExtractEffectContext` | 从 Cue 上下文提取扩展效果上下文 | 提取失败即进入原生回退，不抛异常 |
| `FGameDamageResult::GetDominantActualChannel` | 取实际伤害最大的主伤害通道 | 强化链路主通道唯一来源，不可用 `MatchedTagName` 替代 |
| `FGameDamageResult::TotalActualDamage` | 取实际总伤害作为信号幅度 | 与 `RawMagnitude` 不可混用 |
| `FGameGameplayEffectContext::GetFeedbackEventId` | 读取预测事件 ID 用于跨端去重 | 仅扩展上下文路径透传，原生回退无此字段 |
| `UGameFeedbackBlueprintLibrary::BroadcastFeedback` | 投递信号到反馈总线 | GameThread；目标 `Feedback.Channel.Play` |

## `UGameplayCueNotify_GameFeedback`

```cpp
UCLASS()
class GAMEGAMEPLAY_API UGameplayCueNotify_GameFeedback : public UGameplayCueNotify_Static {
    GENERATED_BODY()

public:
    UGameplayCueNotify_GameFeedback() = default;

    bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;
};
```

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `OnExecute_Implementation` | `MyTarget` 目标实体、`Parameters` Cue 参数上下文 | `bool` | 目标为空返回 `false` 并记录 Verbose 日志；否则组装信号并广播，返回 `true` |

- **线程**：Cue 执行由 GAS 在 GameThread 调度，内部无锁、无跨线程状态。
- **生命周期**：无实例成员，多个 Cue 可共享同一 CDO；不持有 Actor 或资产引用。
- **失败语义**：仅目标为空是失败路径，此时不广播任何信号；信号字段缺失只产生默认值，不报错。
- **实体解析**：统一使用 `Parameters.GetInstigator()` 与 `Parameters.GetEffectCauser()`，显式字段为空时由引擎回落到 `EffectContext`。

## 信号还原

### 强化链路（搭载 `FGameGameplayEffectContext`）

当 `FGameGameplayEffectContext::ExtractEffectContext(Parameters.EffectContext)` 成功时：

| `FGameFeedbackSignal` 字段 | 来源 |
| :--- | :--- |
| `Target` | 形参 `MyTarget`；适配器在空时回退 `HitResult.GetActor()` |
| `Instigator` / `DamageCauser` | `Parameters.GetInstigator()` / `Parameters.GetEffectCauser()` |
| `HitResult` | 优先 `EffectContext.GetHitResult()`，缺省回退 `Parameters.Location` 与 `Normal` |
| `PrimaryChannel` | `FGameDamageResult::GetDominantActualChannel()`，即实际伤害最大的主通道 |
| `Magnitude` | `FGameDamageResult::TotalActualDamage` |
| `FeedbackTags` | `FGameDamageResult::FeedbackTags` |
| `bLethal` | `FeedbackTags` 是否含 `TAG_Feedback_State_Lethal` |
| `ImpactDirection` | `DamageCauser` 速度归一化；速度近零时回退 `HitResult.ImpactNormal` |
| `TraceDirection` | `HitResult.TraceEnd - HitResult.TraceStart` 归一化 |
| `PredictionEventId` | `FGameGameplayEffectContext::GetFeedbackEventId()` |

- **线程**：字段提取与广播同处 GameThread。
- **失败语义**：上下文缺失时整条强化链路不执行，自动进入原生回退，不抛异常。

### 原生回退（普通 `FGameplayEffectContext`）

| `FGameFeedbackSignal` 字段 | 来源 |
| :--- | :--- |
| `Target` / `Instigator` / `DamageCauser` / `HitResult` | 形参与 `Parameters` 解析结果 |
| `PrimaryChannel` | `Parameters.MatchedTagName` |
| `Magnitude` | `Parameters.RawMagnitude` |
| `ImpactDirection` | `Parameters.Normal` 非零时取其值，否则取 `HitResult.ImpactNormal` |
| `TraceDirection` | `HitResult.TraceEnd - HitResult.TraceStart` 归一化 |

> 原生回退仅为兼容无扩展上下文的 Cue；常规伤害表现必须以强化链路的主伤害通道为准。

## 最小示例

```cpp
FGameGameplayEffectContext* GameContext = new FGameGameplayEffectContext();
GameContext->AddInstigator(Instigator, DamageCauser);
GameContext->SetActualDamage(150.0f);
GameContext->SetActualChannelDamage(SlashTag, 100.0f);
GameContext->SetActualChannelDamage(BluntTag, 50.0f);
GameContext->SetFeedbackEventId(321);

FGameplayCueParameters CueParameters;
CueParameters.EffectContext = FGameplayEffectContextHandle(GameContext);
CueParameters.MatchedTagName = BluntTag;

Cue->OnExecute_Implementation(Target, CueParameters);
```

上例最终 `PrimaryChannel` 为 `SlashTag`（主伤害通道），而非 Cue 的 `BluntTag`。

## 常见错误与边界

- **空目标**：`MyTarget == nullptr` 时直接返回 `false` 且不广播，调用方需保证目标有效。
- **误用 Cue Tag**：强化链路必须以主伤害通道作为 `PrimaryChannel`；用 `MatchedTagName` 覆盖会丢失多通道结算的主次信息。
- **幅度取错**：强化链路的 `Magnitude` 是实际总伤害，不是 `RawMagnitude`；两者不可混用。
- **预测 ID 仅限扩展上下文**：`PredictionEventId` 只在 `FGameGameplayEffectContext` 路径透传，原生回退路径没有该字段。
- **命中信息缺失**：无 `HitResult` 时使用 `Location` / `Normal` 兜底，冲击方向可能与真实轨迹不同。
- **表现配置分散**：震动、闪白、音效等参数应在 `UGameFeedbackRegistry` / `UGameFeedbackDefinition` 集中维护，不要在 Cue 蓝图堆叠 Niagara 或音效组件。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [伤害结算管线](../Architecture/DamageExecutionPipeline.md)
- [快速上手](../Guides/QuickStart.md)
