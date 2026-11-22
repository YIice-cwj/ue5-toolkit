# RemoteLogging 远程日志管道 API 参考

`GameLogging` 插件提供零网络耦合的全局日志捕获与总线分发管道。它由解耦数据载荷 `FGameRemoteLogEntry`、挂载到 `GLog` 的无锁环形输出设备 `FGameRemoteLogOutputDevice` 以及常驻子系统 `UGameLoggingSubsystem` 组成。

---

## 包含头文件

```cpp
#include "Remote/GameRemoteLogTypes.h"
#include "Remote/GameRemoteLogOutputDevice.h"
#include "Remote/GameLoggingSubsystem.h"
```

---

## 1. 核心标签与数据结构

### 消息总线标签：`TAG_Game_Log_Remote`
- **常量定义**：`GAMELOGGING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Game_Log_Remote);`
- **Tag 路径**：`"Game.Log.Remote"`
- **声明位置**：`Plugins/GameLogging/Source/GameLogging/Public/Remote/GameRemoteLogTypes.h`
- **用途**：通过 `UGameplayMessageSubsystem` 向全工程解耦广播结构化日志条目时所使用的标准固定通道。

### 结构体：`FGameRemoteLogEntry`
```cpp
USTRUCT(BlueprintType)
struct GAMELOGGING_API FGameRemoteLogEntry {
    GENERATED_BODY()

public:
    FGameRemoteLogEntry() = default;

    FGameRemoteLogEntry(
        FName InCategory,
        uint8 InVerbosity,
        FString InMessage,
        double InTimestamp,
        int64 InFrameNumber
    );

    [[nodiscard]] ELogVerbosity::Type GetVerbosity() const noexcept;
    [[nodiscard]] bool operator==(const FGameRemoteLogEntry& Other) const noexcept;

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logging")
    FName Category;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logging")
    uint8 Verbosity{static_cast<uint8>(ELogVerbosity::Log)};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logging")
    FString Message;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logging")
    double Timestamp{0.0};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logging")
    int64 FrameNumber{0};
};
```

#### 字段与契约说明
- **`Category`**：日志所属分类名称（如 `LogGame`、`LogCombat`）；
- **`Verbosity`**：日志详细级别（`uint8` 存储，构造函数自动按 `ELogVerbosity::VerbosityMask` 清除非级别标志位）；可通过 `GetVerbosity()` 获取 `ELogVerbosity::Type`；
- **`Message`**：日志正文字符串；`operator==` 采用**精确大小写敏感匹配**（`ESearchCase::CaseSensitive`）；
- **`Timestamp`**：日志触发时钟秒数（`FPlatformTime::Seconds()`）；
- **`FrameNumber`**：日志发生时的引擎全局帧序号（`GFrameCounter`）。

---

## 2. 输出设备：`FGameRemoteLogOutputDevice`

继承自 Unreal 原生 `FOutputDevice`，挂载至全局 `GLog`（`FOutputDeviceRedirector`）。

### 特性与保护机制
1. **私有 TLS 重入守卫**：内部由模块私有类 `Game::Log::Private::FRemoteLogReentrancyScope` 守卫（支持嵌套无损恢复），拦截捕获与总线广播期间的二次重入雪崩；
2. **级别与控制符过滤**：自动拦截 `ELogVerbosity::NoLogging` 及 `ELogVerbosity::SetColor`（配色控制符），并通过 `std::atomic<uint8> MinVerbosity`（默认 `Warning`）快速无锁过滤低级别日志；
3. **L1 内联零泄漏黑名单**：通过 `FRWLock` 保护 `TArray<FName, TInlineAllocator<8>> IgnoredCategories`，默认预置忽略 `LogGameMessage`、`LogGameNetService` 与 `LogGameNetCore`，8 项以内零堆分配且彻底零快照泄漏；
4. **延迟平台时钟采集**：3 参 `Serialize`（及 `Time < 0.0` 调用）仅在通过全部过滤与容量预留后才调用 `FPlatformTime::Seconds()`；
5. **MPMC 无锁环形队列**：复用 `GameCore::TLockFreeRingBuffer<FGameRemoteLogEntry, MAX_CAPACITY>`（`MAX_CAPACITY = 2048`，堆分配单次持有防栈溢出），每条日志零节点 `new`/`delete`；
6. **严格 CAS 容量守恒**：入队前通过 CAS 原子预留容量槽位，严格杜绝多线程突发写入突破软上限 `MaxCapacity`，溢出项精确累加至 `DroppedCount`；支持并发安全 `Clear()` 与无监听者零分配 `DiscardBatch()`。

### 公共接口
```cpp
class GAMELOGGING_API FGameRemoteLogOutputDevice final : public FOutputDevice {
public:
    static constexpr int32 MAX_CAPACITY = 2048;

    FGameRemoteLogOutputDevice();
    ~FGameRemoteLogOutputDevice() override;

    [[nodiscard]] bool CanBeUsedOnMultipleThreads() const override;

    int32 DrainBatch(TArray<FGameRemoteLogEntry>& OutBatch, int32 MaxCount);
    int32 DiscardBatch(int32 MaxCount);

    void SetMinVerbosity(ELogVerbosity::Type InMinVerbosity) noexcept;
    [[nodiscard]] ELogVerbosity::Type GetMinVerbosity() const noexcept;

    void SetMaxCapacity(int32 InCapacity) noexcept;
    [[nodiscard]] int32 GetMaxCapacity() const noexcept;

    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] int32 GetQueuedCount() const noexcept;
    [[nodiscard]] int64 GetDroppedCount() const noexcept;

    void AddIgnoredCategory(FName CategoryName);
    void RemoveIgnoredCategory(FName CategoryName);
    [[nodiscard]] bool IsCategoryIgnored(FName CategoryName) const;

    void Clear();
};
```

---

## 3. 常驻子系统：`UGameLoggingSubsystem`

继承自 `UGameInstanceSubsystem`，跨关卡 Seamless Travel 全生命周期常驻。

### 核心生命周期与运行机制
- **CDO 零堆分配**：默认构造函数保持 `OutputDevice` 为空；
- **`Initialize`**：显式声明 `Collection.InitializeDependency<UGameplayMessageSubsystem>()` 保障消息总线初始化就绪；创建捕获设备并应用预设配置；若处于启用态则挂载 `GLog` 并注册 `FTSTicker`；
- **`Deinitialize`**：停止 `FTSTicker` 与 `GLog` 捕获，调用 `FlushQueue()` 尝试刷新残留尾日志（总线仍活跃时送达监听器，否则安全丢弃），最后释放缓冲与设备（提示：UE 5.8 `FSubsystemCollectionBase::Deinitialize()` 按 `SubsystemMap` 正序卸载，若业务需在 `GameInstance::Shutdown()` 前确保最后一批日志送达远端，建议在关停前显式调用一次 `FlushQueue()`）；
- **零空转启用控制**：调用 `SetRemoteLoggingEnabled(false)` 时同步注销 `FTSTicker` 并从 `GLog` 解挂，禁用态零每帧开销；
- **零分配排空与广播**：空队列 1 次原子判空短路；无监听者时原地调用 `DiscardBatch` 零 `TArray` 分配；有监听者时复用成员 `ReusableBatch` 稳态零堆分配，且广播全程受 `FRemoteLogReentrancyScope` 保护（同时阻止回调内递归 `FlushQueue()`）；
- **`FlushQueue`**：按有界预算循环分批排空至队列见底（或达单次预算上限防无限生产循环）。

### 公共接口
```cpp
UCLASS()
class GAMELOGGING_API UGameLoggingSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    static constexpr int32 DEFAULT_DRAIN_PER_TICK = 50;

    [[nodiscard]] static UGameLoggingSubsystem& Get(const UObject* WorldContextObject);
    [[nodiscard]] static UGameLoggingSubsystem* GetPtr(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "GameLogging|Remote")
    void SetRemoteLoggingEnabled(bool bEnabled);

    UFUNCTION(BlueprintPure, Category = "GameLogging|Remote")
    bool IsRemoteLoggingEnabled() const noexcept;

    void SetMinVerbosity(ELogVerbosity::Type InMinVerbosity) noexcept;
    [[nodiscard]] ELogVerbosity::Type GetMinVerbosity() const noexcept;

    void SetMaxDrainBatchPerTick(int32 InMaxDrain) noexcept;
    [[nodiscard]] int32 GetMaxDrainBatchPerTick() const noexcept;

    void SetMaxQueueCapacity(int32 InCapacity) noexcept;
    [[nodiscard]] int32 GetMaxQueueCapacity() const noexcept;

    UFUNCTION(BlueprintCallable, Category = "GameLogging|Remote")
    void AddIgnoredCategory(FName CategoryName);

    UFUNCTION(BlueprintCallable, Category = "GameLogging|Remote")
    void RemoveIgnoredCategory(FName CategoryName);

    UFUNCTION(BlueprintPure, Category = "GameLogging|Remote")
    bool IsCategoryIgnored(FName CategoryName) const;

    UFUNCTION(BlueprintPure, Category = "GameLogging|Remote")
    int32 GetQueuedCount() const noexcept;

    UFUNCTION(BlueprintPure, Category = "GameLogging|Remote")
    int64 GetDroppedCount() const noexcept;

    int32 FlushQueue();

    UFUNCTION(BlueprintCallable, Category = "GameLogging|Remote")
    void ClearQueue();
};
```

#### 获取与配置子系统示例
```cpp
if (UGameLoggingSubsystem* LoggingSubsystem = UGameLoggingSubsystem::GetPtr(this)) {
    LoggingSubsystem->SetMinVerbosity(ELogVerbosity::Error);
    LoggingSubsystem->AddIgnoredCategory(FName(TEXT("LogThirdPartyNoise")));
}
```

---

## 4. 下游监听与集成范例

外部网络插件（如 `GameNetService`）仅需依赖 `GameMessage` 与 `GameLogging` 的轻量头 `Remote/GameRemoteLogTypes.h`，即可消费远程日志：

```cpp
#include "Message/GameplayMessageSubsystem.h"
#include "Remote/GameRemoteLogTypes.h"

UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
FGameplayMessageListenerHandle ListenerHandle = MessageSubsystem.RegisterListener<FGameRemoteLogEntry>(
    TAG_Game_Log_Remote,
    [](const FGameRemoteLogEntry& Entry) {
        const FString Formatted = FString::Printf(
            TEXT("[%s][%s] %s"),
            *Entry.Category.ToString(),
            ToString(Entry.GetVerbosity()),
            *Entry.Message
        );
        SendLogPacketToServer(Formatted, Entry.Timestamp, Entry.FrameNumber);
    }
);
```
