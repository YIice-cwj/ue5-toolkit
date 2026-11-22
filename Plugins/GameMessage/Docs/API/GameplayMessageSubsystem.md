# GameMessage Gameplay 消息总线 API 参考

`UGameplayMessageSubsystem` 是基于 `GameplayTag` 层级寻址与 `UScriptStruct` 强类型校验的全局解耦消息通信中枢。

头文件：
- `#include "Message/GameplayMessageSubsystem.h"`
- `#include "Message/FGameplayMessageListenerHandle.h"`
- `#include "Message/GameplayMessageTypes.h"`
- `#include "Message/MessageErrorCodes.h"`
- `#include "Message/AsyncAction_ListenForGameplayMessage.h"`
基类：`UGameInstanceSubsystem`

---

## 1. 核心概念、枚举与错误码

### 1.1 `CGameplayMessageStruct`
C++20 编译期载荷约束，要求目标类型为 `USTRUCT` 或已特化 `TBaseStructure<T>` 的引擎核心结构体（如 `FVector`）：
```cpp
template <typename T>
concept CGameplayMessageStruct = requires {
    { TBaseStructure<std::remove_cvref_t<T>>::Get() } -> std::convertible_to<const UScriptStruct*>;
};
```

### 1.2 `EGameplayMessageMatch`
标签匹配规则：
```cpp
UENUM(BlueprintType)
enum class EGameplayMessageMatch : uint8 {
    ExactMatch,   // 严格精确匹配（仅当广播通道与监听通道完全相同时触发）
    PartialMatch, // 层级父子匹配（广播通道与监听通道相同或其子标签时均会触发）
};
```

### 1.3 `Game::MessageErrorCodes`
统一配合 `Game::ModuleId::Message (7)` 写入 `FGameError`：

| 常量 | 数值 | 语义说明 |
|---|---:|---|
| `InvalidChannel` | `7001` | 消息通道 `GameplayTag` 无效或为空 |
| `InvalidStructType` | `7002` | 消息载荷 `UScriptStruct` 反射元数据为空 |
| `InvalidPayload` | `7003` | 消息载荷指针或任务闭包为空 |
| `SubsystemShuttingDown` | `7004` | 消息子系统尚未初始化、正在关闭或已注销 |

### 1.4 `FGameplayMessageListenerHandle`
监听器 RAII 句柄（支持安全拷贝、哈希容器存储、跨线程退订与子系统 `Deinitialize()` 即刻失效感知）：
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

- 内部共享不可变注册态与原子 `ListenerId`，任意线程调用 `IsValid()` 与 `Unregister()` 均无数据竞争；
- **最后一份副本析构时**自动注销；也可提前调用 `Unregister()` 显式注销并清空句柄；
- 所属 `UGameplayMessageSubsystem::Deinitialize()` 执行后，关联运行纪元立即作废，存量句柄 `IsValid()` 即刻返回 `false` 且不会误伤后续 `Initialize()` 新建的同 ID 监听器。

---

## 2. `UGameplayMessageSubsystem` 类接口

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
        FGameplayTag Channel,
        TOwner* ListenerObject,
        void (TOwner::*CallbackMethod)(FGameplayTag, const TMessageStructType&),
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );

    template <CGameplayMessageStruct TMessageStructType, typename TOwner>
    [[nodiscard]] FGameplayMessageListenerHandle RegisterListener(
        FGameplayTag Channel,
        TOwner* ListenerObject,
        void (TOwner::*CallbackMethod)(const TMessageStructType&),
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );

    template <CGameplayMessageStruct TMessageStructType>
    [[nodiscard]] FGameplayMessageListenerHandle RegisterListener(
        FGameplayTag Channel,
        TFunction<void(FGameplayTag, const TMessageStructType&)> Callback,
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );

    template <CGameplayMessageStruct TMessageStructType>
    [[nodiscard]] FGameplayMessageListenerHandle RegisterListener(
        FGameplayTag Channel,
        TFunction<void(const TMessageStructType&)> Callback,
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );

    [[nodiscard]] int32 GetListenerCount() const;
};
```

### 核心语义约定
- **路由一致性**：`HasListener(Channel)` 与 `BroadcastMessage(Channel, ...)` 共享同一套精确 + 祖先 `PartialMatch` 查找逻辑，广播端可在打包昂贵载荷前安全短路。
- **结构体继承多态**：广播派生 `USTRUCT` 时，订阅基类 `USTRUCT` 的监听器通过 `UScriptStruct::IsChildOf` 自动命中。
- **重入安全**：广播期间允许回调内自退订、注销其他监听器（被注销监听当轮即刻静默，绝不 UAF）、动态注册新监听器（自下一轮生效）以及嵌套广播。
- **统一退订入口**：外部退订统一通过 `FGameplayMessageListenerHandle::Unregister()` 或句柄作用域析构触发，不暴露底层 `int64 ListenerId`。
- **上下文直通**：`Get` / `GetPtr` 支持传入 `UWorld`、`UGameInstance`、`UGameInstanceSubsystem`（无 `UWorld` 时亦可直通）以及任意具备 `World` 的场景 `UObject`。

---

## 3. 蓝图异步监听节点 (`UAsyncAction_ListenForGameplayMessage`)

```cpp
UCLASS()
class GAMEMESSAGE_API UAsyncAction_ListenForGameplayMessage : public UBlueprintAsyncActionBase {
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable)
    FAsyncGameplayMessageDelegate OnMessageReceived;

    UFUNCTION(BlueprintCallable, Category = "GameMessage|Message", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"))
    static UAsyncAction_ListenForGameplayMessage* ListenForGameplayMessages(
        UObject* WorldContextObject,
        FGameplayTag Channel,
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );

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

- 内部以引擎 `FStructOnScope` 按 `GetMinAlignment()` 缓存最近一条载荷，并在 `AddReferencedObjects` 中完整追踪载荷内 `UPROPERTY() UObject*` 与动态 `UScriptStruct` 引用，在 `BeginDestroy()` 中兜底释放监听句柄与结构体内存；
- `Activate()` 幂等，重复激活不会重复注册监听器；无效通道或缺失子系统时自动调用 `SetReadyToDestroy()`。

---

## 4. 调用规范与代码样例

### 4.1 声明自定义消息体
```cpp
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "MyMessageTypes.generated.h"

USTRUCT(BlueprintType)
struct FPlayerKillMessage {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    TWeakObjectPtr<AActor> Killer;

    UPROPERTY(BlueprintReadWrite)
    TWeakObjectPtr<AActor> Victim;

    UPROPERTY(BlueprintReadWrite)
    int32 ScoreGained = 100;
};
```

### 4.2 注册监听器（RAII 句柄持有）
```cpp
FGameplayMessageListenerHandle KillHandle = UGameplayMessageSubsystem::Get(this).RegisterListener<FPlayerKillMessage>(
    FGameplayTag::RequestGameplayTag(TEXT("Event.Combat.Kill")),
    [this](FGameplayTag Channel, const FPlayerKillMessage& Payload) {
        OnKillRecorded(Channel, Payload.ScoreGained);
    }
);

KillHandle.Unregister();
```

### 4.3 同步广播与跨线程入队
```cpp
FPlayerKillMessage Msg;
Msg.Killer = PlayerA;
Msg.Victim = PlayerB;
Msg.ScoreGained = 100;

UGameplayMessageSubsystem& Bus = UGameplayMessageSubsystem::Get(this);
if (Bus.HasListener(TAG_Event_Combat_Kill)) {
    Bus.BroadcastMessage(TAG_Event_Combat_Kill, Msg);
}

// 后台工作线程安全入队，自动合并调度至 GameThread 串行派发
Bus.EnqueueMessage(TAG_Event_Combat_Kill, MoveTemp(Msg));
```
