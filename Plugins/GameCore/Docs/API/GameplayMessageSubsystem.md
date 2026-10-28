# GameCore Gameplay 消息总线 API 参考

`UGameplayMessageSubsystem` 是基于 GameplayTag 层级寻址与 `UScriptStruct` 强类型校验的解耦消息通信中枢。

头文件：
- `#include "Message/GameplayMessageSubsystem.h"`
- `#include "Message/GameplayMessageTypes.h"`
- `#include "Message/AsyncAction_ListenForGameplayMessage.h"`
基类：`UGameInstanceSubsystem`

---

## 1. 核心枚举与类型

### 1.1 `EGameplayMessageMatch`
标签匹配规则：
```cpp
UENUM(BlueprintType)
enum class EGameplayMessageMatch : uint8 {
    ExactMatch,   // 严格精确匹配（仅当前 Tag 完全一致才触发）
    PartialMatch, // 层级父子匹配（广播通道为监听通道的子标签时亦会触发）
};
```

### 1.2 `FGameplayMessageListenerHandle`
监听器句柄（RAII 守卫），用于控制退订生命周期：
```cpp
struct GAMECORE_API FGameplayMessageListenerHandle {
    void Unregister();
    [[nodiscard]] bool IsValid() const noexcept;
};
```

---

## 2. `UGameplayMessageSubsystem` 类接口

```cpp
UCLASS()
class GAMECORE_API UGameplayMessageSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    static UGameplayMessageSubsystem& Get(const UObject* WorldContextObject);
    static UGameplayMessageSubsystem* GetPtr(const UObject* WorldContextObject);

    /**
     * @brief 广播强类型消息
     * @tparam FMessageStructType 必须是带有 USTRUCT() 的反射结构体
     * @param Channel 寻址通道 GameplayTag
     * @param Message 消息结构体实例
     */
    template <typename FMessageStructType>
    void BroadcastMessage(FGameplayTag Channel, const FMessageStructType& Message);

    /**
     * @brief 注册成员函数监听器（仅接收通道与载荷）
     */
    template <typename FMessageStructType, typename TOwner>
    [[nodiscard]] FGameplayMessageListenerHandle RegisterListener(
        FGameplayTag Channel,
        TOwner* ListenerObject,
        void (TOwner::*CallbackMethod)(FGameplayTag, const FMessageStructType&),
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );

    /**
     * @brief 注册成员函数监听器（仅接收载荷）
     */
    template <typename FMessageStructType, typename TOwner>
    [[nodiscard]] FGameplayMessageListenerHandle RegisterListener(
        FGameplayTag Channel,
        TOwner* ListenerObject,
        void (TOwner::*CallbackMethod)(const FMessageStructType&),
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );

    /**
     * @brief 注册通用 Lambda / TFunction 监听器
     */
    template <typename FMessageStructType>
    [[nodiscard]] FGameplayMessageListenerHandle RegisterListener(
        FGameplayTag Channel,
        TFunction<void(FGameplayTag, const FMessageStructType&)> Callback,
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );

    /**
     * @brief 手动注销指定通道上的监听器 ID（升级 64 位整型，防止长时间运行符号溢出）
     */
    void UnregisterListener(FGameplayTag Channel, int64 ListenerId);

    /**
     * @brief 检查指定通道是否存在任何监听器
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Message")
    bool HasListener(FGameplayTag Channel, EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch) const;

    /**
     * @brief 查询当前已注册的监听器总数
     */
    [[nodiscard]] int32 GetListenerCount() const;
};
```

### 监听句柄的拷贝语义

`FGameplayMessageListenerHandle` 内部持有 `TSharedPtr` 注册对象，可安全拷贝；**最后一份副本析构时**才通过共享引用自动注销，`Unregister()` 可提前显式注销并清空句柄。

---

## 3. 蓝图异步监听节点 (`UAsyncAction_ListenForGameplayMessage`)

在蓝图中，可通过 `ListenForGameplayMessages` 异步节点直接监听通道消息：

```cpp
UCLASS()
class GAMECORE_API UAsyncAction_ListenForGameplayMessage : public UBlueprintAsyncActionBase {
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable)
    FAsyncGameplayMessageDelegate OnMessageReceived;

    UFUNCTION(BlueprintCallable, Category = "GameCore|Message", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"))
    static UAsyncAction_ListenForGameplayMessage* ListenForGameplayMessages(
        UObject* WorldContextObject,
        FGameplayTag Channel,
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );
};
```

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
        UE_LOG(LogTemp, Log, TEXT("Kill recorded: Score +%d"), Payload.ScoreGained);
    }
);

// 手动退订（或随 KillHandle 离开作用域析构自动退订）
KillHandle.Unregister();
```

### 4.3 广播消息
```cpp
FPlayerKillMessage Msg;
Msg.Killer = PlayerA;
Msg.Victim = PlayerB;
Msg.ScoreGained = 100;

UGameplayMessageSubsystem::Get(this).BroadcastMessage(
    FGameplayTag::RequestGameplayTag(TEXT("Event.Combat.Kill")),
    Msg
);
```
