# GameGameplay GAS GameplayCue 打击感桥接 API 参考

`UGameplayCueNotify_GameFeedback` 是将 Unreal Engine GAS 原生 `GameplayCue` 事件统一收敛至 `GameFeedback` 表现中枢的静态无状态桥接器。

- **头文件**：`#include "Bridge/UGameplayCueNotify_GameFeedback.h"`
- **基类**：`UGameplayCueNotify_Static`
- **形态**：静态无状态 Cue 处理器
- **核心职责**：提取 `FGameplayCueParameters` 中的伤害上下文与命中数据，组装为标准的 `FGameFeedbackSignal` 信号，调用 `UGameFeedbackBlueprintLibrary::BroadcastFeedback` 派发至表现总线，实现 GAS 战斗与视听表现彻底解耦。

---

## 1. 类接口声明

```cpp
UCLASS()
class GAMEGAMEPLAY_API UGameplayCueNotify_GameFeedback : public UGameplayCueNotify_Static {
    GENERATED_BODY()

public:
    UGameplayCueNotify_GameFeedback() = default;

    /**
     * @brief 触发 GameplayCue 执行回调
     * @param MyTarget 目标受击实体
     * @param Parameters Cue 携带的参数上下文（含 EffectContext、Location、Normal、Magnitude 等）
     * @return 执行是否成功（目标为空返回 false，其余返回 true）
     */
    virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;
};
```

---

## 2. 核心执行逻辑与双轨适配

`OnExecute_Implementation` 提供自适应双轨信号解析：

### 2.1 强化链路：搭载 `FGameGameplayEffectContext`
当 `Parameters.EffectContext` 为工程扩展的 `FGameGameplayEffectContext` 时：
1. 提取完整的命中结果 `FHitResult`（优先使用 Context 内的命中信息，缺省回退到 `Parameters.Location` 与 `Normal`）；
2. 构造 `FGameDamageContext`（注入 `Instigator`、`DamageCauser` 与 `HitResult`）；
3. 通过 `GameContext->ToDamageResult()` 还原包含通道实际伤害、主导通道及反馈标签（如致死、暴击）的 `FGameDamageResult`；
4. 调用 `UGameFeedbackAdapter::MakeSignalFromDamage` 自动提取 `PrimaryChannel`、`Magnitude`、`FeedbackTags`、`bLethal` 与计算 `ImpactDirection`；
5. 透传跨端预测事件 ID `PredictionEventId = GameContext->GetFeedbackEventId()`，保证客户端本地预测表现与服务器网络回执能够精准去重。

### 2.2 基础链路：原生 `FGameplayEffectContext` 回退
当未搭载特定扩展上下文（如通用 GameplayCue 或原生 GE）时：
1. 直接使用 `Parameters.Instigator`、`Parameters.EffectCauser`、`MyTarget` 绑定实体；
2. 提取 `Parameters.RawMagnitude` 作为表现强度；
3. 将 `Parameters.MatchedTagName` 作为主通道标签；
4. 计算轨迹方向 `TraceDirection = (HitResult.TraceEnd - HitResult.TraceStart).GetSafeNormal()`。

最后，桥接器统一调用：
```cpp
UGameFeedbackBlueprintLibrary::BroadcastFeedback(MyTarget, Signal);
```
将纯表现信号投递至世界总线 `Feedback.Channel.Play`，交由各能力监听者（镜头震动、顿帧、闪白、粒子、音效、权威网络派发）各自独立消费。

---

## 3. 工程接入示例

在项目内容浏览器中：
1. 新建蓝图类，继承自 `GameplayCueNotify_GameFeedback`；
2. 指定其 GameplayCue 标签（例如 `GameplayCue.Combat.Hit`）；
3. 对应技能或效果施加携带该 Cue 的 GameplayEffect；
4. 所有打击感参数（震动等级、受击闪白、音效特效）均在 `UGameFeedbackRegistry` 与 `UGameFeedbackDefinition` 中集中维护，无需在每个 Cue 蓝图中分散堆砌 Niagara 或音效组件。
