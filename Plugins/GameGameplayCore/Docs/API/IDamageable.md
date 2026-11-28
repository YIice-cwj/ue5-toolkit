# GameGameplayCore 可受击接口 API 参考

> **一句话**：所有能够接收伤害并产生打击反馈的实体契约，攻击端不感知受击方的具体类型。
> **头文件**：`Damage/IDamageable.h`；反射类 `UDamageable`，接口类 `IDamageable` · **线程**：GameThread 同步调用与结算
> **失败语义**：不返回错误码；默认实现返回空 `FGameDamageResult`，调用前须确认目标实现该接口

## 30 秒速览

- **契约**：`UINTERFACE(MinimalAPI, BlueprintType, Blueprintable)` + `BlueprintNativeEvent` 的 `ApplyDamageContext`。
- **同步**：受击方必须在函数返回前完成减免、扣血与结果回填，调用端无需异步反查。
- **调用**：C++ 使用 `IDamageable::Execute_ApplyDamageContext`，蓝图使用 `Apply Damage Context` 节点。
- **默认实现**：未覆写时返回空结果，不产生任何伤害或反馈。
- **扩展**：GAS 角色、Mass 适配器、可破坏物、载具机关均可实现，新增目标类型零修改攻击端。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `IDamageable::ApplyDamageContext` | 受击契约，同步完成减免、扣血并回填结果 | 必须 GameThread 同步返回，不得异步扣血 |
| `IDamageable::ApplyDamageContext_Implementation` | C++ 默认实现 | 未覆写时返回空结果，表现为零伤害 |
| `IDamageable::Execute_ApplyDamageContext` | C++ 统一调用入口，正确分派蓝图覆写 | 调用前须 `Implements<UDamageable>()`，否则断言 / UB |
| `Implements<UDamageable>()` | 校验目标是否实现受击接口 | 未实现时调用会触发断言或未定义行为 |
| 蓝图节点 `Apply Damage Context` | 蓝图侧调用入口，返回 `FGameDamageResult` | 蓝图事件未调用父实现则不执行默认空逻辑 |

## 接口声明

```cpp
UINTERFACE(MinimalAPI, BlueprintType, Blueprintable)
class UDamageable : public UInterface {
    GENERATED_BODY()
};

class GAMEGAMEPLAYCORE_API IDamageable {
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Game|Damage")
    FGameDamageResult ApplyDamageContext(const FGameDamageContext& Context);

    virtual FGameDamageResult ApplyDamageContext_Implementation(const FGameDamageContext& Context) {
        (void)Context;
        return FGameDamageResult();
    }
};
```

| 项 | 说明 |
| :--- | :--- |
| 参数 `Context` | 伤害分发上下文，包含通道、命中信息、发起者 / 造成者与标签 |
| 返回 `FGameDamageResult` | 打击反馈，属性结算在返回前完成 |
| 默认实现 | 返回空结果，`TotalActualDamage == 0.0f` 且无通道、无标签 |
| 反射要求 | 目标 Actor / 对象必须实现 `UDamageable` |

## 调用约定

### C++ 端

```cpp
if (TargetActor != nullptr && TargetActor->Implements<UDamageable>()) {
    const FGameDamageResult Result =
        IDamageable::Execute_ApplyDamageContext(TargetActor, DamageContext);
}
```

| 步骤 | 要求 |
| :--- | :--- |
| 判空 | 目标指针非空 |
| 接口校验 | `Implements<UDamageable>()` 为 `true` |
| 调用 | 必须走 `Execute_ApplyDamageContext`，以正确分派蓝图覆写 |
| 结果 | 同步取得 `FGameDamageResult`，可直接驱动表现 |

### 蓝图端

- 在事件图表中拉出目标 Actor 引脚，搜索并调用 `Apply Damage Context` 节点。
- 节点入参：`Context`（`FGameDamageContext`）；返回值：`Return Value`（`FGameDamageResult`）。
- 蓝图覆写事件时实现的是 `ApplyDamageContext` 对应的原生事件，未调用父实现则不会执行默认空逻辑。

## 实现者规范

- **纯同步结算**：减免、护甲计算、扣血必须在 `ApplyDamageContext_Implementation` 内完成，并把实际结果写入 `FGameDamageResult`。
- **表现异步分离**：受击动画蒙太奇、死亡布娃娃、音效可通过委托异步广播，但不得阻塞返回。
- **免疫与未命中**：无敌状态返回 `TotalActualDamage = 0.0f`，并通过 `Result.AddFeedbackTag(...)` 注入 `Damage.Feedback.Immune` 或具体受击表现标签。

### 最小示例

```cpp
FGameDamageResult AMyTarget::ApplyDamageContext_Implementation(const FGameDamageContext& Context) {
    const FGameplayTag DamagePhysical = FGameplayTag::RequestGameplayTag(TEXT("Damage.Channel.Physical"));
    const FGameplayTag FeedbackImmune = FGameplayTag::RequestGameplayTag(TEXT("Damage.Feedback.Immune"));
    const FGameplayTag FeedbackHit = FGameplayTag::RequestGameplayTag(TEXT("Damage.Feedback.Hit"));

    FGameDamageResult Result;

    if (bInvulnerable) {
        Result.AddFeedbackTag(FeedbackImmune);
        return Result;
    }

    const float Incoming = Context.GetChannelDamage(DamagePhysical);
    float ActualDelta = 0.0f;
    StatComponent->ModifyStat(HealthTag, -Incoming, ActualDelta);

    Result.SetActualDamage(-ActualDelta);
    Result.AddActualChannelDamage(DamagePhysical, -ActualDelta);
    Result.AddFeedbackTag(FeedbackHit);
    return Result;
}
```

## 常见错误与边界

- **未校验接口**：对未实现 `UDamageable` 的对象调用 `Execute_ApplyDamageContext` 会触发断言或未定义行为，调用前必须 `Implements<UDamageable>()`。
- **直接调用实现**：绕过 `Execute_` 直接调用 `ApplyDamageContext` 会跳过蓝图覆写分派，受击逻辑可能不执行。
- **依赖默认实现**：未覆写时默认返回空结果，表现为“打不掉血”，需在实现类或蓝图中覆写。
- **异步扣血**：把属性结算推迟到下一帧或异步任务会导致返回结果与实际伤害不一致，违反同步契约。
- **免疫表达**：免疫不是错误码，而是 `TotalActualDamage = 0.0f` 加反馈标签；调用端应据此区分“未命中 / 免疫 / 零伤害”。
- **蓝图覆写**：蓝图事件未调用父实现时，C++ 默认实现不会执行，空结果逻辑需自行保证。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [闭环伤害管线](../Architecture/DamagePipeline.md)
- [伤害数据类型参考](./DamageTypes.md)
- [快速上手](../Guides/QuickStart.md)
