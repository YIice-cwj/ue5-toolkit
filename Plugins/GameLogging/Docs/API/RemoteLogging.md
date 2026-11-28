# RemoteLogging 远程日志管道 API 参考

> **一句话**：把 `GLog` 日志捕获为 `FGameRemoteLogEntry`，经无锁环形队列由 `UGameLoggingSubsystem` 广播到 `TAG_Game_Log_Remote` 的消息总线管道。
> **头文件**：`Remote/GameRemoteLogTypes.h`、`Remote/GameRemoteLogOutputDevice.h`、`Remote/GameLoggingSubsystem.h`
> **线程**：`Serialize` 可被多工作线程并发调用；子系统生命周期、Ticker 排空与广播在 GameThread
> **失败语义**：不抛异常；队列满时写入计入 `DroppedCount`，无监听者时零分配丢弃

## 30 秒速览

- **数据契约**：`FGameRemoteLogEntry` 是 `USTRUCT(BlueprintType)` 值类型；固定通道为 `TAG_Game_Log_Remote`（`"Game.Log.Remote"`）。
- **采集**：`FGameRemoteLogOutputDevice` 挂载到 `GLog`，默认只捕获 `Warning` 及以上，并预置防自激黑名单。
- **防重入**：私有 TLS 守卫 `Game::Log::Private::FRemoteLogReentrancyScope` 拦截捕获与广播期间的二次重入。
- **缓冲**：`TLockFreeRingBuffer<FGameRemoteLogEntry, 2048>` MPMC 队列，CAS 预留容量，满溢计入 `DroppedCount`。
- **常驻**：`UGameLoggingSubsystem` 继承 `UGameInstanceSubsystem`，默认每帧排空 `50` 条；禁用后注销 Ticker，零空转。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `FGameRemoteLogEntry` | 远程日志条目值类型 | 纯值类型可跨线程复制；固定通道 `TAG_Game_Log_Remote` |
| `FGameRemoteLogOutputDevice::Serialize` | 日志拦截并写入队列 | 可多工作线程并发调用；先过滤、过 TLS 重入守卫与黑名单 |
| `FGameRemoteLogOutputDevice::DrainBatch` | 并发安全批量出队 | 返回实际排出条数 |
| `FGameRemoteLogOutputDevice::DiscardBatch` | 无监听者时原地丢弃 | 零 `TArray` 分配 |
| `FGameRemoteLogOutputDevice::SetMaxCapacity` | 调整队列容量 | 钳制至 `[1, 2048]`，非正值忽略 |
| `UGameLoggingSubsystem::SetRemoteLoggingEnabled` | 启停远程日志 | 禁用即注销 Ticker 并从 `GLog` 解挂，零每帧开销 |
| `UGameLoggingSubsystem::FlushQueue` | 按有界预算排空队列 | GameThread 调用；重入被 TLS 守卫拦截 |
| `UGameLoggingSubsystem::GetPtr` | 取远程日志子系统指针 | 未就绪返回 `nullptr` |

## 数据契约

```cpp
GAMELOGGING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Game_Log_Remote);

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

| 字段 / 接口 | 说明 |
| :--- | :--- |
| `Category` | 日志分类名（如 `LogGame`、`LogCombat`） |
| `Verbosity` | `uint8` 存储详细级别；带参构造按 `ELogVerbosity::VerbosityMask` 清除非级别标志位 |
| `GetVerbosity()` | 取强类型 `ELogVerbosity::Type` |
| `Message` | 日志正文 |
| `Timestamp` | 日志时钟秒数（`FPlatformTime::Seconds()`） |
| `FrameNumber` | 日志发生时的引擎帧序号（`GFrameCounter`） |
| `operator==` | 正文精确大小写敏感匹配，时间戳按浮点容差比较 |

- **线程**：纯值类型，可跨线程复制；`FString` 复制遵循 UE 容器语义。
- **生命周期**：无外部资源持有；随批次数组或监听回调入参存活。
- **失败语义**：无错误返回；无监听者时条目由子系统零分配丢弃。

## 输出设备 `FGameRemoteLogOutputDevice`

```cpp
class GAMELOGGING_API FGameRemoteLogOutputDevice final : public FOutputDevice {
public:
    static constexpr int32 MAX_CAPACITY = 2048;

    FGameRemoteLogOutputDevice();
    ~FGameRemoteLogOutputDevice() override;

    [[nodiscard]] bool CanBeUsedOnMultipleThreads() const override;

    void Serialize(
        const TCHAR* V,
        ELogVerbosity::Type Verbosity,
        const class FName& Category,
        const double Time
    ) override;

    void Serialize(
        const TCHAR* V,
        ELogVerbosity::Type Verbosity,
        const class FName& Category
    ) override;

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

| 接口 | 说明 |
| :--- | :--- |
| `Serialize` | 日志拦截入口；先做级别 / 控制符过滤、TLS 重入守卫与黑名单判断，通过后再入队 |
| `DrainBatch(OutBatch, MaxCount)` | 并发安全批量出队，返回实际排出条数 |
| `DiscardBatch(MaxCount)` | 无监听者快速路径，原地丢弃并返回条数，零 `TArray` 分配 |
| `SetMinVerbosity` / `GetMinVerbosity` | 调整最低严重程度，默认 `Warning` |
| `SetMaxCapacity` / `GetMaxCapacity` | 容量钳制至 `[1, MAX_CAPACITY]`，非正值忽略 |
| `AddIgnoredCategory` / `RemoveIgnoredCategory` / `IsCategoryIgnored` | 维护忽略黑名单，`NAME_None` 忽略 |
| `GetQueuedCount` / `GetDroppedCount` | 查询队列条目数与累计丢弃数 |
| `Clear` | 并发安全清空队列并重置丢弃计数 |

- **防重入**：TLS 守卫支持嵌套无损恢复，阻断日志捕获与总线广播触发的递归日志雪崩。
- **过滤**：拦截 `ELogVerbosity::NoLogging` 与 `ELogVerbosity::SetColor`；`FRWLock` 保护 `TArray<FName, TInlineAllocator<8>> IgnoredCategories`，默认忽略 `LogGameMessage`、`LogGameNetService`、`LogGameNetCore`。
- **线程**：`CanBeUsedOnMultipleThreads` 恒为 `true`；多线程可并发 `Serialize`、`DrainBatch`、`DiscardBatch` 与 `Clear`。
- **生命周期**：由 `UGameLoggingSubsystem` 持有；析构清空未消费队列。
- **失败语义**：容量预留失败不阻塞调用线程，条目被丢弃并累加 `DroppedCount`；延迟时钟读取只在通过全部过滤后执行。

## 常驻子系统 `UGameLoggingSubsystem`

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

- **初始化**：`Initialize` 显式声明 `InitializeDependency<UGameplayMessageSubsystem>()`，创建捕获设备、应用预设配置，并在启用态挂载 `GLog` 与注册 `FTSTicker`。
- **卸载**：`Deinitialize` 停止 Ticker 与捕获，调用 `FlushQueue()` 刷新残留尾日志，随后释放缓冲与设备。
- **零空转**：`SetRemoteLoggingEnabled(false)` 同步注销 Ticker 并从 `GLog` 解挂；禁用态无每帧开销。
- **排空**：空队列 1 次原子判空短路；无监听者走 `DiscardBatch` 零分配；有监听者复用 `ReusableBatch` 并受重入守卫保护。
- **`FlushQueue`**：按有界预算循环排空至队列见底或达单次上限，返回实际处理条数。
- **线程**：生命周期、Ticker、`FlushQueue` 与广播在 GameThread；配置接口按 GameThread 调用设计。
- **失败语义**：总线未就绪或已销毁时残留日志安全丢弃；`GetPtr` 未就绪返回 `nullptr`，`Get` 断言有效。

```cpp
if (UGameLoggingSubsystem* LoggingSubsystem = UGameLoggingSubsystem::GetPtr(this)) {
    LoggingSubsystem->SetMinVerbosity(ELogVerbosity::Error);
    LoggingSubsystem->AddIgnoredCategory(FName(TEXT("LogThirdPartyNoise")));
}
```

> UE 5.8 的 `FSubsystemCollectionBase::Deinitialize()` 按 `SubsystemMap` 正序卸载；若必须在 `GameInstance::Shutdown()` 前确保最后一批日志送达远端，请在关停前显式调用一次 `FlushQueue()`。

## 下游监听示例

订阅方仅需依赖 `GameMessage` 与轻量头 `Remote/GameRemoteLogTypes.h`：

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

## 常见错误与边界

- **默认阈值**：设备默认只捕获 `Warning` 及以上；`Log` / `Display` 需显式下调 `MinVerbosity`。
- **默认黑名单**：`LogGameMessage`、`LogGameNetService`、`LogGameNetCore` 默认不采集，避免日志管道自激。
- **队列满溢**：写入不阻塞，超出 `MaxCapacity` 的条目被丢弃，需监控 `GetDroppedCount()`。
- **重入保护**：回调内再次 `FlushQueue()` 会被 TLS 守卫拦截，不会递归广播。
- **卸载时序**：`GameInstance` 关停前如需送达尾日志，先显式 `FlushQueue()`；仅依赖 `Deinitialize` 可能因子系统卸载顺序错过监听者。
- **订阅依赖**：订阅模块必须依赖 `GameMessage`，否则无法包含 `GameplayMessageSubsystem` 头。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [快速上手指南](../Guides/QuickStart.md)
- [最佳实践与性能指南](../Guides/BestPractices.md)
