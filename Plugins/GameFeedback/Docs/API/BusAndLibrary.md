# GameFeedback 表现总线与广播函数库 API 参考

本文档列出 `GameFeedback` 插件中总线通道标签、载荷结构体以及调用方唯一交互的蓝图函数库。

- 头文件：
  - `#include "Bus/GameFeedbackChannels.h"`
  - `#include "Bus/GameFeedbackMessages.h"`
  - `#include "Bus/GameFeedbackBlueprintLibrary.h"`
  - `#include "Adapter/GameFeedbackAdapter.h"`

---

## 1. 总线通道标签 (`GameFeedbackChannels.h`)

框架基于 `NativeGameplayTags` 定义了 3 个专有原生通道标签：

| 标签常量 | 字符串标识 | 职责 |
|---|---|---|
| `TAG_Feedback_Channel_Play` | `"Feedback.Channel.Play"` | 本地表现广播通道（单机或服务端主发起，驱动所有本地监听者与网络派发） |
| `TAG_Feedback_Channel_Network` | `"Feedback.Channel.Network"` | 客户端网络回执回灌通道（由 Receiver 接收后转发，携带 `bSuppressed` 标记） |
| `TAG_Feedback_Channel_Clear` | `"Feedback.Channel.Clear"` | 状态重置与清除通道（死亡、复活、切关时快速复原 Overlay 材质与定格时间） |

---

## 2. 总线消息载荷 (`GameFeedbackMessages.h`)

所有载荷结构体均为支持蓝图的 USTRUCT：

```cpp
/**
 * @brief 本地表现广播载荷
 */
USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackMessage {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FGameFeedbackSignal Signal;

    /** 可选覆盖资产；为空时由监听者按注册表动态解析 */
    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TObjectPtr<UGameFeedbackDefinition> OverrideDefinition = nullptr;
};

/**
 * @brief 网络回执广播载荷（携带本地预测抑制标记）
 */
USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackNetworkMessage {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FGameFeedbackSignal Signal;

    /** 服务端权威解析出的最终 Definition */
    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TObjectPtr<UGameFeedbackDefinition> ResolvedDefinition = nullptr;

    /** 本地预测已播放而需要抑制重复表现的标志 */
    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    bool bSuppressed = false;
};

/**
 * @brief 清除指定实体表现状态的广播载荷
 */
USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackClearMessage {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TWeakObjectPtr<AActor> Target = nullptr;
};
```

---

## 3. 广播入口函数库 (`UGameFeedbackBlueprintLibrary`)

调用方（技能、武器、战斗伤害等）与表现层之间的**唯一公开契约**。

```cpp
UCLASS()
class GAMEFEEDBACK_API UGameFeedbackBlueprintLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()

public:
    /**
     * @brief 广播一次打击表现信号（由总线上的各能力监听者自行消费）
     * @param WorldContextObject 世界上下文对象
     * @param Signal 纯表现信号
     * @param Definition 指定的表现资产（可选，为空时按注册表解析）
     */
    UFUNCTION(BlueprintCallable, Category = "GameFeedback", meta = (AdvancedDisplay = "Definition"))
    static void BroadcastFeedback(
        const UObject* WorldContextObject,
        const FGameFeedbackSignal& Signal,
        const UGameFeedbackDefinition* Definition = nullptr);

    /**
     * @brief 广播清除指定实体的表现状态（还原受击闪白 Overlay 与解除顿帧）
     * @param WorldContextObject 世界上下文对象
     * @param Target 待重置的目标实体
     */
    UFUNCTION(BlueprintCallable, Category = "GameFeedback")
    static void BroadcastClearFeedback(const UObject* WorldContextObject, AActor* Target);

    /**
     * @brief 将携带服务端最终 Definition 的网络回执回灌本地总线（由 Receiver 内部调用）
     * @param WorldContextObject 世界上下文对象
     * @param Signal 还原后的表现信号
     * @param ResolvedDefinition 服务端最终 Definition
     * @param bSuppressed 本地预测已播放而需要抑制重复表现
     */
    static void BroadcastNetworkFeedback(
        const UObject* WorldContextObject,
        const FGameFeedbackSignal& Signal,
        const UGameFeedbackDefinition* ResolvedDefinition,
        bool bSuppressed);
};
```

---

## 4. 伤害契约转换适配器 (`UGameFeedbackAdapter`)

当业务工程使用 `GameGameplayCore` 伤害管线时，无需手写赋值逻辑，适配器提供一键标准化转换：

```cpp
UCLASS()
class GAMEFEEDBACK_API UGameFeedbackAdapter : public UBlueprintFunctionLibrary {
    GENERATED_BODY()

public:
    /**
     * @brief 从伤害分发上下文与结算结果提取标准视听表现信号
     * @param InTarget 受击目标实体（显式传入；若为空则自动从 HitResult 提取）
     * @param Context 伤害上下文（提供 Instigator, DamageCauser, HitResult 等）
     * @param Result 伤害结算结果（提供 TotalActualDamage, FeedbackTags, Channels 等）
     * @return 转换后的视听表现信号
     */
    UFUNCTION(BlueprintPure, Category = "GameFeedback|Adapter")
    static FGameFeedbackSignal MakeSignalFromDamage(
        AActor* InTarget,
        const FGameDamageContext& Context,
        const FGameDamageResult& Result);
};
```

### 转换映射规则：
1. **目标实体绑定**：`Signal.Target = InTarget ? InTarget : Context.HitResult.GetActor()`；
2. **冲击方向判定**：若伤害造成者（`DamageCauser`）存在且速度非零，优先以速度归一化向量作为 `ImpactDirection`；否则使用 `Context.HitResult.ImpactNormal`；
3. **主通道选取**：自动读取 `Result.GetDominantActualChannel()`（结算伤害数值最大的通道）；
4. **致死标记**：自动检查 `Result.FeedbackTags.HasTag(TAG_Feedback_State_Lethal)` 填充 `Signal.bLethal`；
5. **弹道方向**：自动计算 `(TraceEnd - TraceStart).GetSafeNormal()` 赋予 `Signal.TraceDirection`。
