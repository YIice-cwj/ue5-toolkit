# GameMessage API 参考

> **一句话**：基于 `GameplayTag` 层级寻址与 `UScriptStruct` 强类型校验的全局解耦消息总线。
> **头文件**：`Message/GameplayMessageSubsystem.h`、`Message/FGameplayMessageListenerHandle.h`、`Message/GameplayMessageTypes.h`、`Message/MessageErrorCodes.h`、`Message/AsyncAction_ListenForGameplayMessage.h`
> **线程**：`BroadcastMessage` / `RegisterListener` / `FlushPendingMessages` 仅限 GameThread；`EnqueueMessage` 可被任意工作线程调用，经无锁 MPSC 队列合并至 GameThread 派发
> **失败语义**：`TGameStatusResult<void>` + `Game::MessageErrorCodes`（归属 `Game::ModuleId::Message`）

## 30 秒速览

- **零 CDO 开销**：`UGameplayMessageSubsystem` 以 Pimpl 隐藏内部实现，构造函数不分配堆内存，公共头文件不暴露原子量与 MPSC 队列。
- **层级寻址**：`ExactMatch` 仅同通道命中；`PartialMatch` 同时命中同通道与祖先通道监听器。
- **跨线程**：`EnqueueMessage` 线程安全入队，主线程 `FlushPendingMessages` 串行派发。
- **重入安全**：每轮广播基于监听器快照派发，回调内退订即刻静默、注册下一轮生效、允许嵌套广播。
- **生命周期**：`FGameplayMessageListenerHandle` 为 RAII 句柄；子系统 `Deinitialize()` 以 `ActiveEpoch` 使存量句柄立即失效。
- **蓝图**：`UAsyncAction_ListenForGameplayMessage` 以 `FStructOnScope` 对齐缓存并完整参与 GC 追踪。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `UGameplayMessageSubsystem::BroadcastMessage` | 同步广播强类型消息 | 仅 GameThread；无效通道返回 `7001` |
| `UGameplayMessageSubsystem::EnqueueMessage` | 跨线程入队，主线程派发 | 任意工作线程安全；经无锁 MPSC 队列合并 |
| `UGameplayMessageSubsystem::RegisterListener` | 注册监听并返回 RAII 句柄 | 仅 GameThread；宿主 GC 后回调自动跳过 |
| `UGameplayMessageSubsystem::FlushPendingMessages` | 串行派发待处理队列 | 仅 GameThread；返回派发条数 |
| `UGameplayMessageSubsystem::HasListener` | 查询通道是否存在监听器 | 精确 + 祖先 `PartialMatch` 一致查找 |
| `FGameplayMessageListenerHandle::Unregister` | 显式退订并清空句柄 | 任意线程安全；最后副本析构时自动注销 |
| `UAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages` | 蓝图异步监听节点 | `Activate()` 幂等；无效通道自动销毁 |
| `UAsyncAction_ListenForGameplayMessage::GetPayload` | 提取最近一次载荷 | 需匹配 `UScriptStruct`；`FStructOnScope` 参与 GC |

## 1. 核心概念与错误码

### `CGameplayMessageStruct`

```cpp
template <typename T>
concept CGameplayMessageStruct = requires {
    { TBaseStructure<std::remove_cvref_t<T>>::Get() } -> std::convertible_to<const UScriptStruct*>;
};
```

约束载荷必须为 `USTRUCT`，或已特化 `TBaseStructure<T>` 的引擎核心结构体（如 `FVector`）。

### `EGameplayMessageMatch`

```cpp
UENUM(BlueprintType)
enum class EGameplayMessageMatch : uint8 {
    ExactMatch,   // 广播通道与监听通道完全相同
    PartialMatch, // 广播通道等于监听通道，或为其子标签
};
```

### `Game::MessageErrorCodes`

| 常量 | 数值 | 语义 |
| :--- | ---: | :--- |
| `InvalidChannel` | `7001` | 通道 `GameplayTag` 无效或为空 |
| `InvalidStructType` | `7002` | 载荷 `UScriptStruct` 反射元数据为空 |
| `InvalidPayload` | `7003` | 载荷指针或任务闭包为空 |
| `SubsystemShuttingDown` | `7004` | 子系统未初始化、正在关闭或已注销 |

## 2. 监听句柄 `FGameplayMessageListenerHandle`

```cpp
USTRUCT(BlueprintType)
struct GAMEMESSAGE_API FGameplayMessageListenerHandle {
    GENERATED_BODY()
public:
    FGameplayMessageListenerHandle() = default;
    void Unregister();
    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] bool operator==(const FGameplayMessageListenerHandle& Other) const noexcept;
    [[nodiscard]] bool operator!=(const FGameplayMessageListenerHandle& Other) const noexcept;
    [[nodiscard]] friend uint32 GetTypeHash(const FGameplayMessageListenerHandle& Handle) noexcept;
};
```

- 可安全拷贝并存入 `TSet` / `TMap`；内部共享不可变注册态与原子 `ListenerId`，任意线程 `IsValid()` / `Unregister()` 无数据竞争。
- **最后一份副本析构时**自动注销；`Unregister()` 可提前显式注销并清空句柄。
- 子系统 `Deinitialize()` 后关联 `ActiveEpoch` 作废，存量句柄 `IsValid()` 立即返回 `false`，且不会误伤重建后的同 ID 监听器。

## 3. `UGameplayMessageSubsystem`

`UGameInstanceSubsystem` 门面，Pimpl 隐藏注册表与派发队列；CDO 零堆分配。

| 参数 / 返回 | 语义 |
| :--- | :--- |
| `Channel` | 目标或监听 `GameplayTag`；无效返回 `7001` |
| `Message` | 满足 `CGameplayMessageStruct` 的强类型载荷 |
| `ListenerObject` | 成员函数监听宿主；以弱引用追踪，宿主 GC 后自动跳过 |
| `Callback` | `TFunction` 回调，可接收通道 Tag + 载荷或仅载荷 |
| `MatchRule` | 默认 `ExactMatch`，可选 `PartialMatch` |
| `FGameplayMessageListenerHandle` | RAII 订阅句柄，唯一退订入口 |
| `TGameStatusResult<void>` | 失败携带 `Game::ModuleId::Message` 与 7001-7004 错误码 |

```cpp
UCLASS()
class GAMEMESSAGE_API UGameplayMessageSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()
public:
    static UGameplayMessageSubsystem& Get(const UObject* WorldContextObject);
    static UGameplayMessageSubsystem* GetPtr(const UObject* WorldContextObject);
    void Initialize(FSubsystemCollectionBase& Collection) override;
    void Deinitialize() override;

    UFUNCTION(BlueprintPure, Category = "GameMessage|Message")
    bool HasListener(FGameplayTag Channel) const;

    template <CGameplayMessageStruct TMessageStructType>
    TGameStatusResult<void> BroadcastMessage(FGameplayTag Channel, const TMessageStructType& Message);

    template <CGameplayMessageStruct TMessageStructType>
    TGameStatusResult<void> EnqueueMessage(FGameplayTag Channel, TMessageStructType Message);

    int32 FlushPendingMessages();

    template <CGameplayMessageStruct TMessageStructType, typename TOwner>
    [[nodiscard]] FGameplayMessageListenerHandle RegisterListener(
        FGameplayTag Channel, TOwner* ListenerObject,
        void (TOwner::*CallbackMethod)(FGameplayTag, const TMessageStructType&),
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch);

    template <CGameplayMessageStruct TMessageStructType, typename TOwner>
    [[nodiscard]] FGameplayMessageListenerHandle RegisterListener(
        FGameplayTag Channel, TOwner* ListenerObject,
        void (TOwner::*CallbackMethod)(const TMessageStructType&),
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch);

    template <CGameplayMessageStruct TMessageStructType>
    [[nodiscard]] FGameplayMessageListenerHandle RegisterListener(
        FGameplayTag Channel, TFunction<void(FGameplayTag, const TMessageStructType&)> Callback,
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch);

    template <CGameplayMessageStruct TMessageStructType>
    [[nodiscard]] FGameplayMessageListenerHandle RegisterListener(
        FGameplayTag Channel, TFunction<void(const TMessageStructType&)> Callback,
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch);

    [[nodiscard]] int32 GetListenerCount() const;
};
```

- **路由一致**：`HasListener` 与 `BroadcastMessage` 共用精确 + 祖先 `PartialMatch` 查找；广播端可在打包昂贵载荷前短路。
- **结构体多态**：广播派生 `USTRUCT` 时，订阅基类 `USTRUCT` 的监听器经 `UScriptStruct::IsChildOf` 命中。
- **重入安全**：广播基于当前监听器快照；回调内自退订、注销其他监听器、动态注册（下一轮生效）与嵌套广播均安全，不产生 UAF。
- **线程与生命周期**：同步广播在调用栈内立即执行；跨线程消息经无锁 MPSC 队列合并唤醒，在 GameThread 派发；`Deinitialize` 关闭入队通道、丢弃残留任务、失活并清空监听器。
- **上下文**：`Get` / `GetPtr` 支持 `UWorld`、`UGameInstance`、`UGameInstanceSubsystem` 及具备 `World` 的任意 `UObject`。

```cpp
USTRUCT(BlueprintType)
struct FPlayerKillMessage {
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) TWeakObjectPtr<AActor> Killer;
    UPROPERTY(BlueprintReadWrite) TWeakObjectPtr<AActor> Victim;
    UPROPERTY(BlueprintReadWrite) int32 ScoreGained = 100;
};

const FGameplayTag KillChannel = FGameplayTag::RequestGameplayTag(TEXT("Event.Combat.Kill"));
UGameplayMessageSubsystem& Bus = UGameplayMessageSubsystem::Get(this);

FGameplayMessageListenerHandle KillHandle = Bus.RegisterListener<FPlayerKillMessage>(
    KillChannel,
    [this](FGameplayTag Channel, const FPlayerKillMessage& Payload) {
        OnKillRecorded(Channel, Payload.ScoreGained);
    }
);

FPlayerKillMessage Msg;
Msg.Killer = PlayerA;
Msg.Victim = PlayerB;
Msg.ScoreGained = 100;
if (Bus.HasListener(KillChannel)) {
    Bus.BroadcastMessage(KillChannel, Msg);
}
Bus.EnqueueMessage(KillChannel, MoveTemp(Msg)); // 任意工作线程可调用

KillHandle.Unregister();
```

## 4. 蓝图异步节点

```cpp
UCLASS()
class GAMEMESSAGE_API UAsyncAction_ListenForGameplayMessage : public UBlueprintAsyncActionBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable)
    FAsyncGameplayMessageDelegate OnMessageReceived;

    UFUNCTION(BlueprintCallable, Category = "GameMessage|Message", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"))
    static UAsyncAction_ListenForGameplayMessage* ListenForGameplayMessages(
        UObject* WorldContextObject, FGameplayTag Channel,
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch);

    static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
    void Activate() override;
    void SetReadyToDestroy() override;
    void BeginDestroy() override;

    UFUNCTION(BlueprintCallable, Category = "GameMessage|Message", meta = (DisplayName = "Cancel Message Listener"))
    void Cancel();

    UFUNCTION(BlueprintCallable, CustomThunk, Category = "GameMessage|Message", meta = (CustomStructureParam = "OutPayload"))
    bool GetPayload(UPARAM(ref) int32& OutPayload);

    template <CGameplayMessageStruct TMessageStructType>
    bool GetPayloadTyped(TMessageStructType& OutPayload) const;
};
```

- **GC 安全**：`ReceivedPayload` 以 `FStructOnScope` 按 `GetMinAlignment()` 缓存最近载荷；`AddReferencedObjects` 追踪载荷内 `UPROPERTY() UObject*` 与动态 `UScriptStruct`；`BeginDestroy()` 兜底释放监听句柄与结构体内存。
- `Activate()` 幂等，重复激活不会重复注册；无效通道或缺失子系统自动 `SetReadyToDestroy()`。
- `OnMessageReceived` 传入实际命中通道，`GetPayload` / `GetPayloadTyped` 提取最近载荷。

## 常见错误与边界

- **线程边界**：`BroadcastMessage`、`RegisterListener` 与 `FlushPendingMessages` 必须在 GameThread；跨线程只能 `EnqueueMessage`。
- **非反射载荷**：不满足 `CGameplayMessageStruct` 的类型在编译期被拦截，不能作为消息体。
- **空通道 / 空载荷**：空通道返回 `InvalidChannel (7001)`；反射元数据或闭包为空返回 `InvalidStructType (7002)` / `InvalidPayload (7003)`。
- **关闭期入队**：子系统未初始化或正在关闭时返回 `SubsystemShuttingDown (7004)`。
- **宿主销毁**：成员函数监听以弱引用持有 `UObject`，宿主被 GC 后回调自动跳过，无需手动退订。
- **句柄失效**：`Deinitialize()` 后旧句柄 `IsValid() == false`；不得依赖旧句柄操作新纪元监听器。

## 相关文档

- [插件 README](../../README.md)
- [架构参考：GameCore 事件与消息双层模型](../../../GameCore/Docs/Architecture/EventAndMessaging.md)
- [消息通信与选型指南](../Guides/MessagingGuide.md)
