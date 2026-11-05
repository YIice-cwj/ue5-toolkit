# RemoteLogging 远程日志管道 API 参考

`GameLogging` 插件提供零网络耦合的全局日志捕获与总线分发管道。它由解耦数据载荷 `FGameRemoteLogEntry`、挂载到 `GLog` 的无锁输出设备 `FGameRemoteLogOutputDevice` 以及常驻子系统 `UGameLoggingSubsystem` 组成。

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
- **常量定义**：`UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Game_Log_Remote);`
- **Tag 路径**：`"Game.Log.Remote"`
- **声明位置**：`Logging/Public/Remote/GameRemoteLogTypes.h`
- **用途**：通过 `UGameplayMessageSubsystem` 向全工程解耦广播结构化日志条目时所使用的标准固定通道。

### 结构体：`FGameRemoteLogEntry`
```cpp
USTRUCT(BlueprintType)
struct GAMELOGGING_API FGameRemoteLogEntry {
    GENERATED_BODY()

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
};
```

#### 字段说明
- **`Category`**：日志所属分类名称（如 `LogGame`、`LogCombat`）；
- **`Verbosity`**：日志详细级别（内部以 `uint8` 存储，蓝图兼容）；可通过 `GetVerbosity()` 获取标准 `ELogVerbosity::Type`；
- **`Message`**：日志格式化后的正文字符串；
- **`Timestamp`**：日志触发时的时钟秒数（`FPlatformTime::Seconds()` 高精度双精度浮点数）；
- **`FrameNumber`**：日志发生时的渲染/引擎全局帧序号（`GFrameCounter`）。

---

## 2. 输出设备：`FGameRemoteLogOutputDevice`

继承自 Unreal 原生 `FOutputDevice`，挂载至全局 `GLog`（`FOutputDeviceRedirector`）。

### 特性与保护机制
1. **重入防范**：内部基于 `thread_local bool bTlsIsLogging`。若在日志捕获、格式化或内存分配过程中触发了间接日志，设备立即短路，彻底根除死锁与无限递归栈溢出；
2. **级别阈值过滤**：内部由 `std::atomic<uint8> MinVerbosity` 守卫，默认仅捕获 `ELogVerbosity::Warning` 及以上级别日志（数值越小越严重），低级别日志在入口处即被快速无锁过滤；
3. **黑名单分类过滤**：通过读写锁 `FRWScopeLock` 保护 `TSet<FName> IgnoredCategories`，支持动态忽略噪音日志分类；
4. **MPSC 无锁队列缓冲**：采用 `TQueue<FGameRemoteLogEntry, EQueueMode::Mpsc>`，支持多个工作线程安全无锁入队；
5. **容量截断与防 OOM**：预设最大队列容量（默认 2048），队列满时安全拒绝入队并递增原子计数器 `DroppedCount`，避免网络阻塞或消费不及时导致内存无限膨胀；
6. **批量单消费者排空**：仅由游戏主线程通过 `DrainBatch` 批量弹出条目，零全局锁竞争。

### 公共接口
```cpp
class GAMELOGGING_API FGameRemoteLogOutputDevice final : public FOutputDevice {
public:
    static constexpr int32 DEFAULT_MAX_CAPACITY = 2048;

    FGameRemoteLogOutputDevice();
    ~FGameRemoteLogOutputDevice() override;

    [[nodiscard]] bool CanBeUsedOnMultipleThreads() const override;

    // 批量排出队列中累积的条目（仅限主线程调用）
    int32 DrainBatch(TArray<FGameRemoteLogEntry>& OutBatch, int32 MaxCount);

    // 动态调整最低严重程度阈值（数值越小越严重，如 Warning 为 3）
    void SetMinVerbosity(ELogVerbosity::Type InMinVerbosity) noexcept;
    [[nodiscard]] ELogVerbosity::Type GetMinVerbosity() const noexcept;

    // 动态调整最大容量限制
    void SetMaxCapacity(int32 InCapacity) noexcept;

    // 查询当前队列积压数量与满溢丢弃累计数
    [[nodiscard]] int32 GetQueuedCount() const noexcept;
    [[nodiscard]] int64 GetDroppedCount() const noexcept;

    // 黑名单过滤管理
    void AddIgnoredCategory(FName CategoryName);
    void RemoveIgnoredCategory(FName CategoryName);

    // 清空当前队列并重置丢弃计数器
    void Clear();
};
```

---

## 3. 常驻子系统：`UGameLoggingSubsystem`

继承自 `UGameInstanceSubsystem`，在游戏实例启动时自动创建，跨关卡 Seamless Travel 全生命周期常驻。

### 核心生命周期与运行机制
- **`Initialize`**：实例化 `FGameRemoteLogOutputDevice`，调用 `GLog->AddOutputDevice` 完成捕获挂载；注册 `FTSTicker` 主线程定时轮询（每帧排空）；
- **`Deinitialize`**：注销 `FTSTicker`，从 `GLog->RemoveOutputDevice` 安全解挂设备；
- **定时排空与广播**：在主线程 `HandleTicker` 回调中，调用 `DrainBatch` 每次最多取走 `MaxDrainBatchPerTick`（默认 50 条）条目，逐条向 `UGameplayMessageSubsystem::BroadcastMessage(TAG_Game_Log_Remote, Entry)` 广播，**本插件零网络耦合**；
- **`FlushQueue`**：立即就地批量排空并广播当前堆积的全部日志（支持单次循环排空至队列见底，适合自动化测试断言或退出前同步）。

### 公共接口
```cpp
UCLASS()
class GAMELOGGING_API UGameLoggingSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    static constexpr int32 DEFAULT_DRAIN_PER_TICK = 50;

    UFUNCTION(BlueprintCallable, Category = "GameLogging|Remote")
    void SetRemoteLoggingEnabled(bool bEnabled);

    UFUNCTION(BlueprintPure, Category = "GameLogging|Remote")
    bool IsRemoteLoggingEnabled() const noexcept;

    void SetMinVerbosity(ELogVerbosity::Type MinVerbosity);
    [[nodiscard]] ELogVerbosity::Type GetMinVerbosity() const noexcept;

    void SetMaxDrainBatchPerTick(int32 InMaxDrain) noexcept;
    [[nodiscard]] int32 GetMaxDrainBatchPerTick() const noexcept;

    // 立即排空并广播当前全部队列，返回实际广播的条目数
    int32 FlushQueue();

    // 获取底层输出设备裸指针（供高级过滤配置或测试断言使用）
    [[nodiscard]] FGameRemoteLogOutputDevice* GetOutputDevice() const noexcept;
};
```

#### 获取子系统实例示例
由于本类为标准 `UGameInstanceSubsystem`，通过引擎原生方式获取：
```cpp
UGameInstance* GameInstance = ...;
if (UGameLoggingSubsystem* LoggingSubsystem = GameInstance->GetSubsystem<UGameLoggingSubsystem>())
{
    LoggingSubsystem->SetMinVerbosity(ELogVerbosity::Error);
}
```

---

## 4. 下游监听与集成范例

外部网络插件（如 `GameNetService`）仅需依赖 `GameplayMessageRuntime` 监听总线，即可消费远程日志，实现架构完全解耦：

```cpp
#include "GameGameplayMessageSubsystem.h"
#include "Remote/GameRemoteLogTypes.h"

// 在网络服务启动时订阅日志频道
UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
FGameplayMessageListenerHandle ListenerHandle = MessageSubsystem.RegisterListener<FGameRemoteLogEntry>(
    TAG_Game_Log_Remote,
    [](FGameplayTag Channel, const FGameRemoteLogEntry& Entry) {
        // 将结构化条目打包为二进制网络数据包并上报
        FString Formatted = FString::Printf(TEXT("[%s][%s] %s"),
            *Entry.Category.ToString(),
            *UEnum::GetValueAsString(Entry.GetVerbosity()),
            *Entry.Message);
        
        SendLogPacketToServer(Formatted, Entry.Timestamp, Entry.FrameNumber);
    }
);
```
