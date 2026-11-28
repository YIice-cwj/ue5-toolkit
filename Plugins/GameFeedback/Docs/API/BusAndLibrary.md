# GameFeedback 表现总线与广播函数库 API 参考

> **一句话**：总线通道标签、消息载荷与调用方唯一广播入口的公共 API。
> **头文件**：`Bus/GameFeedbackChannels.h`、`Bus/GameFeedbackMessages.h`、`Bus/GameFeedbackBlueprintLibrary.h` · **线程**：广播与监听均在 GameThread；网络回执由 Client RPC 在 GameThread 回灌 · **失败语义**：无返回值；`bEnableFeedback` 关闭、总线或 `WorldContextObject` 无效时静默丢弃

## 30 秒速览

- **通道**：`Feedback.Channel.Play` / `Network` / `Clear` 三个原生标签。
- **载荷**：`FGameFeedbackMessage`、`FGameFeedbackNetworkMessage`、`FGameFeedbackClearMessage` 均为蓝图可读写 USTRUCT。
- **入口**：`BroadcastFeedback` 未传 `Definition` 时预解析一次，供全部监听者复用。
- **清理**：`BroadcastClearFeedback` 不受 `bEnableFeedback` 热关断阻断，始终放行复位。
- **网络**：`BroadcastNetworkFeedback` 携带 `bSuppressed` 与 `ReceivingController`，配合同帧去重避免分屏重复表现。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `TAG_Feedback_Channel_Play` | 本地表现广播通道 | 仅 GameThread 监听 |
| `TAG_Feedback_Channel_Network` | 客户端网络回执回灌通道 | 携带 `bSuppressed`，供 Receiver 内部使用 |
| `TAG_Feedback_Channel_Clear` | 状态重置与清除通道 | 不受 `bEnableFeedback` 热关断阻断 |
| `FGameFeedbackMessage` | 本地表现广播载荷 | `OverrideDefinition` 为空时由监听者解析 |
| `FGameFeedbackNetworkMessage` | 网络回执载荷 | `bSuppressed` 时监听者应跳过重复表现 |
| `FGameFeedbackClearMessage` | 清理载荷 | `Target` 为空时不广播 |
| `UGameFeedbackBlueprintLibrary::BroadcastFeedback` | 向 `Play` 通道广播表现 | `bEnableFeedback` 关闭时静默丢弃 |
| `UGameFeedbackBlueprintLibrary::BroadcastClearFeedback` | 向 `Clear` 通道广播复位 | 不受热关断影响；`Target` 为空直接返回 |
| `UGameFeedbackBlueprintLibrary::BroadcastNetworkFeedback` | 回灌网络回执 | 仅供 Receiver 内部；需 GameThread |

## 总线通道标签

| 标签常量 | 字符串标识 | 职责 |
| :--- | :--- | :--- |
| `TAG_Feedback_Channel_Play` | `"Feedback.Channel.Play"` | 本地表现广播通道 |
| `TAG_Feedback_Channel_Network` | `"Feedback.Channel.Network"` | 客户端网络回执回灌通道，携带 `bSuppressed` |
| `TAG_Feedback_Channel_Clear` | `"Feedback.Channel.Clear"` | 状态重置与清除通道 |

## 总线消息载荷

```cpp
USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackMessage {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FGameFeedbackSignal Signal;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TObjectPtr<UGameFeedbackDefinition> OverrideDefinition = nullptr;
};

USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackNetworkMessage {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FGameFeedbackSignal Signal;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TObjectPtr<UGameFeedbackDefinition> ResolvedDefinition = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    bool bSuppressed = false;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TWeakObjectPtr<APlayerController> ReceivingController = nullptr;
};

USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackClearMessage {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TWeakObjectPtr<AActor> Target = nullptr;
};
```

| 结构体 | 字段 | 说明 |
| :--- | :--- | :--- |
| `FGameFeedbackMessage` | `Signal` | 纯表现信号 |
| | `OverrideDefinition` | 可选覆盖资产；为空时由监听者按注册表解析 |
| `FGameFeedbackNetworkMessage` | `Signal` | 服务端还原后的信号 |
| | `ResolvedDefinition` | 服务端权威解析出的最终 Definition |
| | `bSuppressed` | 本地预测已播放，监听者应跳过重复表现 |
| | `ReceivingController` | 非空时仅对应本地玩家组件响应 |
| `FGameFeedbackClearMessage` | `Target` | 待重置表现的实体 |

## 广播入口 `UGameFeedbackBlueprintLibrary`

```cpp
UFUNCTION(BlueprintCallable, Category = "GameFeedback", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "Definition"))
static void BroadcastFeedback(
    const UObject* WorldContextObject,
    const FGameFeedbackSignal& Signal,
    const UGameFeedbackDefinition* Definition = nullptr);

UFUNCTION(BlueprintCallable, Category = "GameFeedback", meta = (WorldContext = "WorldContextObject"))
static void BroadcastClearFeedback(const UObject* WorldContextObject, AActor* Target);

static void BroadcastNetworkFeedback(
    const UObject* WorldContextObject,
    const FGameFeedbackSignal& Signal,
    const UGameFeedbackDefinition* ResolvedDefinition,
    bool bSuppressed,
    APlayerController* ReceivingController = nullptr);
```

| 方法 | 参数 | 返回 | 行为 |
| :--- | :--- | :---: | :--- |
| `BroadcastFeedback` | `WorldContextObject`、`Signal`、可选 `Definition` | `void` | 向 `Play` 通道广播；`Definition` 为空时经 Provider 预解析一次 |
| `BroadcastClearFeedback` | `WorldContextObject`、`Target` | `void` | 向 `Clear` 通道广播；`Target` 为空时直接返回 |
| `BroadcastNetworkFeedback` | `WorldContextObject`、`Signal`、`ResolvedDefinition`、`bSuppressed`、`ReceivingController` | `void` | 向 `Network` 通道回灌网络回执 |

- **线程**：三个入口均须在 GameThread 调用；消息在 GameThread 同步分发给监听者。
- **生命周期**：无内部状态，不持有世界或资产引用；`bEnableFeedback` 在每次广播时读取项目配置。
- **失败语义**：`bEnableFeedback == false` 时 `BroadcastFeedback` 与 `BroadcastNetworkFeedback` 静默返回；`BroadcastClearFeedback` 不受该开关影响；总线或上下文无效时静默丢弃。

```cpp
FGameFeedbackSignal Signal;
Signal.Instigator = GetInstigator();
Signal.DamageCauser = this;
Signal.Target = HitActor;
Signal.HitResult = HitResult;
Signal.Magnitude = 50.0f;
Signal.PrimaryChannel = FGameplayTag::RequestGameplayTag(TEXT("Damage.Channel.Blunt"));

UGameFeedbackBlueprintLibrary::BroadcastFeedback(this, Signal);
```

## 网络回执与分屏去重

- `UGameFeedbackReceiverComponent` 收到 RPC 后还原信号，并通过 `BroadcastNetworkFeedback` 回灌 `Network` 通道。
- `bSuppressed == true` 时，`PlayerComponent`、`WorldComponent` 与 `HitStopService` 整段跳过执行。
- `ReceivingController` 非空时，仅该本地玩家的组件响应；为空时全部本地玩家组件都会响应，分屏下可能重复。
- `UGameFeedbackWorldComponent` 使用同帧栈内联去重表，支持分屏交错到达（A→B→A→B）时只执行一次。
- 网络紧凑报文 `FGameFeedbackEventNet` 的字节预算与字段契约见 [SignalAndActions.md](SignalAndActions.md)。

## 常见错误与边界

- **热关断只拦广播**：`bEnableFeedback` 关闭会丢弃 `BroadcastFeedback` / `BroadcastNetworkFeedback`，但 `BroadcastClearFeedback` 仍会清理残留状态。
- **无效上下文**：`WorldContextObject` 无法取得消息总线时，三个入口均静默返回，不产生错误码。
- **重复预解析**：显式传入 `Definition` 可跳过 Provider 解析；未传且 Provider 不可用时 `OverrideDefinition` 为空，由监听者自行解析。
- **误用网络入口**：`BroadcastNetworkFeedback` 面向 Receiver 内部回灌；玩法层直接调用会绕过网络权威解析。
- **分屏重复**：`ReceivingController` 为空时所有本地玩家组件都会响应，分屏项目应传入实际接收控制器。
- **清理目标为空**：`BroadcastClearFeedback` 对空 `Target` 直接返回，不会广播空清理消息。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [网络同步、裁剪与预测去重](../Architecture/NetworkAndCulling.md)
- [快速上手](../Guides/QuickStart.md)
