# GameFlow 公共 API 参考

> **一句话**：关卡三态流转、代际就绪屏障、加权进度与遥测，以及协程 / 蓝图等待原语。
> **头文件**：`Core/GameFlowTypes.h`、`System/ULoadingSubsystem.h`、`System/FLoadingBarrierToken.h`、`Async/FWaitLoadingCompleteAwaiter.h`、`Blueprint/AsyncAction_WaitForLoadingComplete.h` · **线程**：子系统、屏障与委托均在 GameThread；后台取消经 `FGameThreadTaskRunner::RunOnGameThread` 回投 · **失败语义**：无效上下文返回 `nullptr` / `false`；看门狗或外部释放后 `IsValid() == false`

## 30 秒速览

- **三态流转**：`Idle` → `LoadingMap` → `WaitingBarriers`，由 `ULoadingSubsystem` 统一驱动。
- **代际屏障**：`FLoadingBarrierToken` 为栈上 RAII 令牌，内部持有 `FLoadingBarrierHandle` 代际句柄，防过期复用。
- **进度与遥测**：O(1) 加权进度，结束后广播 `FLoadingTelemetryReport` 并缓存最近一次报告。
- **双轨看门狗**：全局超时（默认 `30s`）与单屏障超时并行；单屏障超时 `> 0` 时独立熔断。
- **等待原语**：`GameFlow::WaitLoadingComplete` 协程与 `UAsyncAction_WaitForLoadingComplete` 蓝图节点。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `ULoadingSubsystem::Get` | 获取关卡加载子系统 | 无效上下文返回 `nullptr`；仅 GameThread |
| `ULoadingSubsystem::OpenLevelWithLoading` | 开启新一轮流转并打开关卡 | 加载中重复调用或 `NAME_None` 告警忽略 |
| `ULoadingSubsystem::AcquireBarrierToken` | 获取栈上 RAII 屏障令牌 | 析构或 `Release()` 自动解除；权重下限 `0.1f` |
| `ULoadingSubsystem::SetMaxLoadingTimeoutSeconds` | 设置全局看门狗超时 | `<= 0` 或非有限值关闭全局超时 |
| `FLoadingBarrierToken::ReportProgress` | 上报屏障进度 | 自动 clamp `[0, 1]`；非有限值或已失效静默忽略 |
| `FLoadingBarrierToken::Release` | 显式释放屏障 | 幂等；重复调用无副作用 |
| `FLoadingBarrierToken::IsValid` | 查询令牌是否仍有效 | 看门狗熔断后立即为 `false` |
| `GameFlow::WaitLoadingComplete` | 协程等待加载完成 | `TimeoutSeconds <= 0` 表示不设超时 |
| `UAsyncAction_WaitForLoadingComplete::Cancel` | 终止蓝图等待节点 | 走 `OnCancelled`，不触发 `OnCompleted` |

## 核心类型

| 符号 | 类型 | 说明 |
| :--- | :--- | :--- |
| `FLoadingBarrierHandle` | `TGameHandle<FLoadingBarrierTag>` | 64 位强类型代际句柄（32 位槽位索引 + 32 位代际号），防过期复用 |
| `ELoadingTransitionPhase` | `UENUM(BlueprintType)` | `Idle`、`LoadingMap`、`WaitingBarriers` |
| `FLoadingBarrierTelemetry` | `USTRUCT(BlueprintType)` | 单屏障耗时：`BarrierName`、`DurationMs`、`Weight`、`bTimedOut` |
| `FLoadingTelemetryReport` | `USTRUCT(BlueprintType)` | 单次流转报告：`LevelName`、`TotalDurationMs`、`MapLoadDurationMs`、`bTimedOut`、`Barriers` |

| 委托 | 签名 | 触发时机 |
| :--- | :--- | :--- |
| `FOnLoadingStateChanged` | `(bool bIsLoading, FName TargetLevelName)` | 真实开始与结束加载时 |
| `FOnLoadingProgressChanged` | `(float Progress, const FText& StepDescription)` | 加权综合进度 `[0, 1]` 变化时 |
| `FOnLoadingTelemetryReport` | `(const FLoadingTelemetryReport& Report)` | 每次流转结束 |
| `FOnLoadingAbortedNative` | `()` | 活跃加载期子系统 `Deinitialize()` 被迫中止时 |

## 关卡加载子系统 `ULoadingSubsystem`

### 获取与跳转

```cpp
UFUNCTION(BlueprintPure, Category = "GameFlow|Loading", meta = (WorldContext = "WorldContextObject"))
static ULoadingSubsystem* Get(const UObject* WorldContextObject);

UFUNCTION(BlueprintCallable, Category = "GameFlow|Loading")
void OpenLevelWithLoading(const FName& LevelName);
```

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `Get` | `WorldContextObject` | 子系统指针 | 支持 `ULoadingSubsystem*`、`UGameInstance*`、Outer 链对象与 `UWorld*`；无效或已回收返回 `nullptr` |
| `OpenLevelWithLoading` | `LevelName` | `void` | 开启新一轮流转并调用 `UGameplayStatics::OpenLevel` |

- **线程**：仅 GameThread。
- **生命周期**：加载中重复调用或 `NAME_None` 告警忽略；收尾广播期间重入会排队至本轮广播完成后顺序发起下一轮，收尾期间二次排队告警忽略。
- **失败语义**：`Get` 对无效上下文返回 `nullptr`；无 `UGameInstance` 的编辑器预览世界不参与流转。

### 屏障管理

```cpp
[[nodiscard]] FLoadingBarrierToken AcquireBarrierToken(
    const FName& BarrierName,
    float InWeight = 1.0f,
    float InTimeoutSeconds = 0.0f);

UFUNCTION(BlueprintCallable, Category = "GameFlow|Loading", meta = (DisplayName = "Acquire Loading Barrier"))
int64 AcquireLoadingBarrierBp(FName BarrierName, float InWeight = 1.0f, float InTimeoutSeconds = 0.0f);

UFUNCTION(BlueprintCallable, Category = "GameFlow|Loading", meta = (DisplayName = "Release Loading Barrier"))
bool ReleaseLoadingBarrierBp(int64 BarrierId);

UFUNCTION(BlueprintCallable, Category = "GameFlow|Loading", meta = (DisplayName = "Report Barrier Progress"))
bool ReportBarrierProgressBp(int64 BarrierId, float Progress, const FText& Description);
```

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `AcquireBarrierToken` | `BarrierName`、`InWeight`、`InTimeoutSeconds` | `FLoadingBarrierToken` | 零堆分配栈上令牌；析构或 `Release()` 自动解除 |
| `AcquireLoadingBarrierBp` | `BarrierName`、`InWeight`、`InTimeoutSeconds` | `int64` | 非零 64 位代际打包句柄 |
| `ReleaseLoadingBarrierBp` | `BarrierId` | `bool` | 代际防重放；无效或重复释放返回 `false` |
| `ReportBarrierProgressBp` | `BarrierId`、`Progress`、`Description` | `bool` | 进度自动 clamp 至 `[0, 1]`；非有限值拒绝并返回 `false` |

- **线程**：仅 GameThread。
- **生命周期**：权重下限 `0.1f`，非有限值回落 `1.0f`；单屏障超时 `> 0` 时即使全局看门狗关闭也独立生效。
- **失败语义**：无效句柄、过期代际或非法进度返回 `false`；同名的多个屏障按代际句柄独立跟踪。

### 状态查询与看门狗

```cpp
bool IsLoading() const noexcept;
ELoadingTransitionPhase GetLoadingPhase() const noexcept;
FName GetTargetLevelName() const noexcept;
float GetLoadingProgress() const noexcept;
bool HasActiveBarriers() const noexcept;
int32 GetActiveBarrierCount() const noexcept;
const FLoadingTelemetryReport& GetLastTelemetryReport() const noexcept;

void SetMaxLoadingTimeoutSeconds(float InTimeoutSeconds);
float GetMaxLoadingTimeoutSeconds() const noexcept;
void CheckWatchdogTimeout();
void CheckWatchdogTimeoutAt(double CurrentTimeSeconds);
```

| 方法 | 返回 | 说明 |
| :--- | :--- | :--- |
| `IsLoading` / `GetLoadingPhase` | `bool` / 枚举 | 是否处于加载或等待屏障态，以及精细阶段 |
| `GetTargetLevelName` / `GetLoadingProgress` | `FName` / `float` | 目标关卡与综合进度；完成后保留 `1.0`，下一轮开始时重置为 `0` |
| `HasActiveBarriers` / `GetActiveBarrierCount` | `bool` / `int32` | 当前未释放屏障状态 |
| `GetLastTelemetryReport` | `const FLoadingTelemetryReport&` | 最近一次流转报告只读引用 |
| `SetMaxLoadingTimeoutSeconds` / `GetMaxLoadingTimeoutSeconds` | `void` / `float` | `<= 0` 或非有限值关闭全局超时，实时联动看门狗定时器 |
| `CheckWatchdogTimeout` / `CheckWatchdogTimeoutAt` | `void` | 平台时钟巡检 / 显式时钟巡检，后者供确定性测试 |

## 屏障令牌 `FLoadingBarrierToken`

```cpp
class GAMEFLOW_API FLoadingBarrierToken {
public:
    FLoadingBarrierToken() noexcept = default;
    FLoadingBarrierToken(const FLoadingBarrierToken&) = delete;
    FLoadingBarrierToken& operator=(const FLoadingBarrierToken&) = delete;
    FLoadingBarrierToken(FLoadingBarrierToken&& Other) noexcept;
    FLoadingBarrierToken& operator=(FLoadingBarrierToken&& Other) noexcept;
    ~FLoadingBarrierToken();

    void Release();
    void ReportProgress(float InProgress, const FText& InDescription = FText::GetEmpty());

    [[nodiscard]] bool IsReleased() const noexcept;
    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] float GetProgress() const noexcept;
    [[nodiscard]] FName GetBarrierName() const noexcept;
    [[nodiscard]] FLoadingBarrierHandle GetBarrierHandle() const noexcept;
};
```

| 方法 | 返回 | 说明 |
| :--- | :--- | :--- |
| `Release()` | `void` | 幂等显式释放屏障 |
| `ReportProgress(InProgress, InDescription)` | `void` | 上报进度并推动全局广播；非有限值或已失效静默忽略 |
| `IsReleased()` | `bool` | 本地令牌是否已释放或被移动 |
| `IsValid()` / `operator bool()` | `bool` | 实时查询子系统槽位；看门狗强制超时后立即为 `false` |
| `GetProgress()` / `GetBarrierName()` / `GetBarrierHandle()` | 对应值 | 查询最近进度、诊断名与代际句柄 |

- **线程**：仅 GameThread。
- **生命周期**：不可拷贝、仅可移动；移动赋值先释放当前旧屏障，自移动安全，源对象重置为空状态；析构自动释放。
- **失败语义**：`Release()` 重复调用无副作用；看门狗熔断或外部释放后 `IsValid() == false`，析构不再重复释放。

```cpp
if (ULoadingSubsystem* Loading = ULoadingSubsystem::Get(this)) {
    Loading->OpenLevelWithLoading(FName(TEXT("/Game/Maps/BattleArena")));

    FLoadingBarrierToken Barrier = Loading->AcquireBarrierToken(
        FName(TEXT("ArenaWarmup")), 2.0f, 10.0f);
    Barrier.ReportProgress(0.5f, NSLOCTEXT("Loading", "Warmup", "正在预热战场资源"));
    Barrier.Release();
}
```

## C++20 协程等待原语

```cpp
namespace GameFlow {
[[nodiscard]] GAMEFLOW_API FWaitLoadingCompleteAwaiter WaitLoadingComplete(
    const UObject* WorldContextObject,
    FCancellationToken CancellationToken = FCancellationToken::None(),
    float TimeoutSeconds = 0.0f);
}
```

| 参数 | 类型 | 说明 |
| :--- | :--- | :--- |
| `WorldContextObject` | `const UObject*` | 世界或实例上下文 |
| `CancellationToken` | `FCancellationToken` | 协同取消令牌，默认不可取消 |
| `TimeoutSeconds` | `float` | 等待超时；`<= 0` 或非有限值表示不设超时 |

- **返回语义**：`true` 表示当前非加载态（零堆分配直通）或加载与全部屏障正常完成；`false` 表示上下文无效、令牌预取消、挂起期同 / 跨线程取消、等待超时或子系统销毁中止。
- **线程**：后台线程触发取消时通过 `FGameThreadTaskRunner::RunOnGameThread` 回投主线程恢复协程；所有委托与定时器在恢复 / 取消路径完整解绑。
- **生命周期**：`TGameCoroTask::Cancel()` 或宿主销毁时立即释放全部委托与定时器，零泄漏。

```cpp
if (co_await GameFlow::WaitLoadingComplete(this, CancellationToken, 15.0f)) {
    EnterBattle();
}
```

## 蓝图异步等待节点

```cpp
UFUNCTION(
    BlueprintCallable,
    meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"),
    Category = "GameFlow|Loading"
)
static UAsyncAction_WaitForLoadingComplete* WaitForLoadingComplete(const UObject* WorldContextObject);
```

| 符号 | 类型 | 说明 |
| :--- | :--- | :--- |
| `UAsyncAction_WaitForLoadingComplete::WaitForLoadingComplete` | 工厂节点 | `WorldContextObject` 隐式绑定 |
| `OnCompleted` | 执行引脚 | 当前非加载态或关卡与全部屏障解除后触发 |
| `OnCancelled` | 执行引脚 | 上下文无效、主动 `Cancel()` 或子系统销毁中止时触发 |
| `Cancel()` | `BlueprintCallable` | 手动终止并清理节点，支持激活前或回调内调用 |

- **线程**：仅 GameThread。
- **生命周期**：`Activate()` 幂等；广播前先解绑委托防重入；`BeginDestroy()` 兜底清理；通过 `Subsystem->GetGameInstance()` 完成 GC 注册保活。
- **失败语义**：取消、无效上下文与子系统销毁统一走 `OnCancelled`，不会触发 `OnCompleted`。

## 运行契约

- **三态流转**：`LoadingPhase` 严格按 `Idle → LoadingMap → WaitingBarriers → Idle` 推进，只有地图加载完成且屏障归零才结束流转。
- **看门狗**：全局超时默认 `30s`；`SetMaxLoadingTimeoutSeconds(<= 0)` 关闭全局超时，但单屏障 `InTimeoutSeconds > 0` 仍独立熔断。
- **多 PIE 隔离**：状态按 `OwningGameInstance` 隔离；无 `GameInstance` 的编辑器预览 / 缩略图世界不参与流转。
- **遥测边界**：只有加载态内完成或超时的屏障进入报告；非加载态释放的屏障不污染遥测。
- **销毁中止**：`Deinitialize()` 在空闲态静默清理；仅在活跃加载期触发 `OnLoadingAbortedNative` 并清空委托。

## 常见错误与边界

- **重复跳转**：加载中再次调用 `OpenLevelWithLoading` 会被告警忽略；需要排队时应在收尾广播期间发起，且只允许一次排队。
- **拷贝令牌**：`FLoadingBarrierToken` 禁止拷贝，误用编译失败；跨作用域转移请使用 `MoveTemp`。
- **看门狗超时**：看门狗强制熔断后 `IsValid()` 立即为 `false`，继续 `ReportProgress` 静默无效。
- **超时参数**：协程 `TimeoutSeconds = 0` 表示无限等待，不是立即超时。
- **编辑器世界**：预览 / 缩略图世界没有 `GameInstance`，`Get` 返回 `nullptr` 且不会发生流转。
- **遥测缺失**：非加载态释放的屏障不会出现在 `GetLastTelemetryReport()` 的明细中。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [快速上手](../Guides/QuickStart.md)
