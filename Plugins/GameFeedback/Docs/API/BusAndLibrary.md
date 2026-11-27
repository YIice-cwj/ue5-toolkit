# GameFeedback 表现总线与广播函数库 API 参考

本文档列出 `GameFeedback` 插件中总线通道标签、载荷结构体以及调用方唯一交互的蓝图函数库。

- 头文件：
  - `#include "Bus/GameFeedbackChannels.h"`
  - `#include "Bus/GameFeedbackMessages.h"`
  - `#include "Bus/GameFeedbackBlueprintLibrary.h"`
  - 伤害契约适配器已归位至 Level 3 插件 `GameGameplay`：`#include "Bridge/UGameFeedbackAdapter.h"`

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
 * @brief 网络回执广播载荷（携带本地预测抑制标记与可选定向接收控制器）
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

    /** 可选定向接收控制器（非空时仅对应本地玩家组件响应，杜绝分屏重复触发） */
    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TWeakObjectPtr<APlayerController> ReceivingController = nullptr;
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
     * @brief 广播一次打击表现信号（未指定 Definition 时自动预解析一次供全部监听者零重复复用）
     * @param WorldContextObject 世界上下文对象（蓝图自动隐式绑定）
     * @param Signal 纯表现信号
     * @param Definition 指定的表现资产（可选，为空时按注册表预解析）
     */
    UFUNCTION(BlueprintCallable, Category = "GameFeedback", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "Definition"))
    static void BroadcastFeedback(
        const UObject* WorldContextObject,
        const FGameFeedbackSignal& Signal,
        const UGameFeedbackDefinition* Definition = nullptr);

    /**
     * @brief 广播清除指定实体的表现状态（不受 bEnableFeedback 热关断阻断，始终放行清理复位）
     * @param WorldContextObject 世界上下文对象（蓝图自动隐式绑定）
     * @param Target 待重置的目标实体
     */
    UFUNCTION(BlueprintCallable, Category = "GameFeedback", meta = (WorldContext = "WorldContextObject"))
    static void BroadcastClearFeedback(const UObject* WorldContextObject, AActor* Target);

    /**
     * @brief 将携带服务端最终 Definition 的网络回执回灌本地总线（由 Receiver 内部调用）
     * @param WorldContextObject 世界上下文对象
     * @param Signal 还原后的表现信号
     * @param ResolvedDefinition 服务端最终 Definition
     * @param bSuppressed 本地预测已播放而需要抑制重复表现
     * @param ReceivingController 可选定向接收该回执的本地玩家控制器（分屏下隔离玩家侧组件重复执行）
     */
    static void BroadcastNetworkFeedback(
        const UObject* WorldContextObject,
        const FGameFeedbackSignal& Signal,
        const UGameFeedbackDefinition* ResolvedDefinition,
        bool bSuppressed,
        APlayerController* ReceivingController = nullptr);
};
```

---

## 4. 伤害契约转换适配器（已归位至 `GameGameplay`）

为严格遵守四级无环插件依赖拓扑（Level 2 的 `GameFeedback` 不依赖 Level 3 的 `GameGameplayCore`），`UGameFeedbackAdapter` 已迁至 `Plugins/GameGameplay/Source/GameGameplay/Public/Bridge/UGameFeedbackAdapter.h`（详见 [GameplayCueFeedbackBridge.md](../../../GameGameplay/Docs/API/GameplayCueFeedbackBridge.md)）。
