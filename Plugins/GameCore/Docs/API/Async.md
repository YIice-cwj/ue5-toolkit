# GameCore 异步调度与协程 API

> **一句话**：后台任务派发、帧预算时间切片、分级节流心跳、协同取消与 C++20 无栈协程挂起原语的公共 API。
> **头文件**：按子域包含 `Async/*.h`，各节标注具体文件 · **线程**：调度内核与子系统 Tick 仅限 GameThread；协程可在 GameThread 与工作线程间切换，恢复线程由挂起原语决定；后台工作体严禁触碰 `UObject` / GC / 引擎单例 · **失败语义**：取消经 `FCancellationToken` 或 `TGameCoroTask::Cancel()` 协作式生效；协程结果 `TGameStatusResult<T>` 取消返回 `Cancelled (6)`、未完成返回 `InvalidState (7)`

## 30 秒速览

- **取消**：`FCancellationToken` 为值语义零锁轮询令牌，`FCancellationTokenSource` 签发令牌并广播 `Cancel()`；`CreateLinkedTokenSource` 支持级联取消。
- **后台任务**：`FGameThreadTaskRunner::Run` 派发工作线程任务，`RunOnGameThread` 将续体投回主线程；`FGameAsyncTaskHandle` 提供 `Cancel` / `Wait` / `Stop`。
- **时间切片**：`FTimeSliceScheduler` 按帧预算（默认 1.5 ms，下限 0.05 ms）分步执行；世界级入口为 `UTimeSlicedTaskSubsystem`。
- **节流心跳**：`FThrottledTickScheduler` 按 `EThrottledRate` 分档并以相位偏移打散同周期回调；世界级入口为 `UThrottledTickerSubsystem`。
- **协程**：`TGameCoroTask<T>` 配合 `Game::Coro::BindContext` / `NextFrame` / `WaitSeconds` / `YieldTimeSlice` / `RunOnBackground` 使用；宿主失效或取消时协程帧在挂起点被销毁。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `FCancellationToken::IsCancellationRequested` | 零锁查询是否已请求取消 | 线程安全；未绑定源的令牌恒为 `false` |
| `FCancellationTokenSource::Cancel` | 触发取消并广播回调 | 首次返回 `true`，重复返回 `false`；回调在触发线程同步派发 |
| `FCancellationTokenSource::CreateLinkedTokenSource` | 组合多令牌级联取消 | 任一输入取消即取消衍生源 |
| `FGameThreadTaskRunner::Run / RunOnGameThread` | 后台任务派发与主线程续体 | 工作体禁止触碰 `UObject` / GC；宿主销毁后丢弃续体 |
| `FGameAsyncTaskHandle::Cancel` | 取消后台任务 | 协作式，仅阻止尚未开始的工作体 |
| `FTimeSliceScheduler::Step` | 按帧预算推进时间切片 | 仅限 GameThread，非线程安全；内置重入防护 |
| `FTimeSliceScheduler::EnqueueTask` | 入队分步任务 | 仅限 GameThread；返回 `0` 表示入队失败 |
| `UTimeSlicedTaskSubsystem::Get` | 取世界级时间切片入口 | 上下文无效触发断言；可能为空时用 `GetPtr()` |
| `FThrottledTickScheduler::RegisterIntervalTick` | 注册定间隔节流心跳 | 仅限 GameThread；`IntervalSeconds <= 0` 视为每帧；返回 `0` 表示注册失败 |
| `TGameCoroTask<T>::BindContext` | 绑定协程宿主与取消令牌 | 宿主失效或取消时挂起点自毁协程帧 |

## 协同取消

### `FCancellationToken` / `FCancellationTokenSource`

头文件：`#include "Async/FCancellationToken.h"`、`#include "Async/FCancellationTokenSource.h"`

```cpp
class GAMECORE_API FCancellationToken {
public:
    [[nodiscard]] bool IsCancellationRequested() const noexcept;
    [[nodiscard]] bool CanBeCancelled() const noexcept;
    FCancellationTokenRegistration Register(TFunction<void()> Callback) const;
    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] static FCancellationToken None() noexcept;
};

class GAMECORE_API FCancellationTokenSource {
public:
    [[nodiscard]] FCancellationToken GetToken() const noexcept;
    [[nodiscard]] bool IsCancellationRequested() const noexcept;
    bool Cancel();

    [[nodiscard]] static TSharedRef<FCancellationTokenSource> CreateLinkedTokenSource(
        const FCancellationToken& TokenA,
        const FCancellationToken& TokenB
    );
    [[nodiscard]] static TSharedRef<FCancellationTokenSource> CreateLinkedTokenSource(
        TConstArrayView<FCancellationToken> Tokens
    );
};
```

| 接口 | 说明 |
| :--- | :--- |
| `IsCancellationRequested()` | 零锁原子读取取消标志；未绑定源的令牌恒为 `false` |
| `CanBeCancelled()` | 令牌是否绑定了可触发的取消源 |
| `Register(Callback)` | 注册取消回调，返回 `FCancellationTokenRegistration`；析构自动注销 |
| `Cancel()` | 首次触发返回 `true`，重复调用返回 `false`；回调在出锁后派发 |
| `CreateLinkedTokenSource(...)` | 任一输入令牌取消即级联取消衍生源，支持双令牌、数组视图与初始化列表重载 |

- **线程**：所有查询与 `Cancel()` 均线程安全；回调在触发取消的线程上同步派发。
- **生命周期**：`FCancellationToken` 可廉价拷贝；`FCancellationTokenSource` 不可拷贝、可移动。
- **失败语义**：向已取消的源注册回调会立即同步调用；空令牌的 `Register` 返回空注销句柄。

```cpp
FCancellationTokenSource UserCancelSource;
FCancellationTokenSource TimeoutCancelSource;

TSharedRef<FCancellationTokenSource> LinkedSource =
    FCancellationTokenSource::CreateLinkedTokenSource(UserCancelSource.GetToken(), TimeoutCancelSource.GetToken());

FCancellationToken CombinedToken = LinkedSource->GetToken();
```

## 后台任务与主线程续体

### `FGameThreadTaskRunner` / `FGameAsyncTaskHandle`

头文件：`#include "Async/FGameThreadTaskRunner.h"`

```cpp
class GAMECORE_API FGameThreadTaskRunner {
public:
    FGameThreadTaskRunner() = delete;

    static FGameAsyncTaskHandle Run(TWeakObjectPtr<UObject> Owner, TFunction<void()> BackgroundWork);
    static FGameAsyncTaskHandle Run(
        TWeakObjectPtr<UObject> Owner,
        FCancellationToken CancellationToken,
        TFunction<void()> BackgroundWork
    );
    static FGameAsyncTaskHandle Run(
        FCancellationToken CancellationToken,
        TFunction<void()> BackgroundWork
    );
    static void RunOnGameThread(TWeakObjectPtr<UObject> Owner, TFunction<void()> GameThreadWork);
    static void RunOnGameThread(TFunction<void()> GameThreadWork);
    static void CancelAllPendingTasks() noexcept;
    [[nodiscard]] static int32 GetInFlightTaskCount() noexcept;
    static bool FlushInFlightTasks(double TimeoutSeconds = 2.0);
};

class GAMECORE_API FGameAsyncTaskHandle {
public:
    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] bool IsCancelled() const noexcept;
    [[nodiscard]] bool IsCompleted() const noexcept;
    bool Cancel();
    void Wait() const;
    bool WaitWithTimeout(FTimespan Timeout) const;
    bool Stop(FTimespan Timeout = FTimespan::FromSeconds(2.0));
};
```

| 参数 | 说明 |
| :--- | :--- |
| `Owner` | 宿主弱引用；派发前与工作线程启动时各校验一次，失效则不执行工作体 |
| `CancellationToken` | 协同取消令牌；已取消则提前短路 |
| `BackgroundWork` | 工作线程工作体，禁止触碰 `UObject` / GC / 引擎单例 |
| 返回 `FGameAsyncTaskHandle` | 未派发时返回空句柄（`IsValid() == false`） |

- **线程**：`Run` 从任意线程调用，工作体在 UE TaskGraph 工作线程执行；`RunOnGameThread` 的续体在 GameThread 执行。
- **生命周期**：宿主销毁后 `RunOnGameThread` 静默丢弃续体，不触发 UAF。
- **失败语义**：`Cancel()` 为协作式，只阻止尚未开始的工作体；已在执行的工作体不会被打断。`FlushInFlightTasks` 返回超时前是否排空。

```cpp
FGameAsyncTaskHandle Handle = FGameThreadTaskRunner::Run(
    this,
    Token,
    [Snapshot = MoveTemp(Snapshot)]() { ComputeOnWorker(Snapshot); }
);

FGameThreadTaskRunner::RunOnGameThread(this, [this]() {
    ApplyResultOnGameThread();
});
```

`CancelAllPendingTasks()` 通过全局取消世代丢弃尚未开始执行或尚未投递回主线程的任务。

## 帧预算时间切片

### `FTimeSliceScheduler` / `UTimeSlicedTaskSubsystem`

头文件：`#include "Async/FTimeSliceScheduler.h"`、`#include "Async/UTimeSlicedTaskSubsystem.h"`、`#include "Async/FTimeSlicedTaskHandle.h"`

```cpp
class GAMECORE_API FTimeSliceScheduler {
public:
    using FStepTaskCallback = TFunction<bool()>;
    using FOwnerValidator = TFunction<bool()>;
    using FBatchIterationCallback = TFunction<void(int32 ItemIndex)>;
    using FBatchCompleteCallback = TFunction<void()>;
    using FClockSource = TFunction<double()>;

    static constexpr double DEFAULT_FRAME_BUDGET_MS = 1.5;
    static constexpr double MIN_FRAME_BUDGET_MS = 0.05;

    void Step(double CustomBudgetSeconds = -1.0);
    void SetFrameBudgetMilliseconds(double BudgetMilliseconds) noexcept;
    [[nodiscard]] double GetFrameBudgetMilliseconds() const noexcept;
    void SetClockSource(FClockSource InClockSource);

    uint64 EnqueueTask(FOwnerValidator InValidator, FStepTaskCallback StepWork);
    uint64 EnqueueTask(TWeakObjectPtr<UObject> Owner, FStepTaskCallback StepWork);
    uint64 EnqueueTask(FStepTaskCallback StepWork);
    uint64 EnqueueBatch(
        TWeakObjectPtr<UObject> Owner,
        int32 TotalCount,
        FBatchIterationCallback PerItemWork,
        FBatchCompleteCallback OnComplete = nullptr
    );
    bool CancelTask(uint64 TaskId);
    [[nodiscard]] bool IsTaskActive(uint64 TaskId) const;
    void ClearAllTasks();
    [[nodiscard]] int32 GetPendingTaskCount() const noexcept;
};
```

```cpp
UCLASS()
class GAMECORE_API UTimeSlicedTaskSubsystem : public UTickableWorldSubsystem {
public:
    FTimeSlicedTaskHandle EnqueueTask(TWeakObjectPtr<UObject> Owner, FStepTaskCallback StepWork);
    FTimeSlicedTaskHandle EnqueueTask(FStepTaskCallback StepWork);
    FTimeSlicedTaskHandle EnqueueBatch(
        TWeakObjectPtr<UObject> Owner,
        int32 TotalCount,
        FBatchIterationCallback PerItemWork,
        FBatchCompleteCallback OnComplete = nullptr
    );
    void SetFrameBudgetMilliseconds(double BudgetMilliseconds) noexcept;
    [[nodiscard]] double GetFrameBudgetMilliseconds() const noexcept;
    void ClearAllTasks();
    [[nodiscard]] int32 GetPendingTaskCount() const noexcept;
    [[nodiscard]] static UTimeSlicedTaskSubsystem& Get(const UObject* WorldContextObject);
    [[nodiscard]] static UTimeSlicedTaskSubsystem* GetPtr(const UObject* WorldContextObject);
};

class GAMECORE_API FTimeSlicedTaskHandle {
public:
    void Cancel();
    [[nodiscard]] bool IsActive() const;
    [[nodiscard]] uint64 GetTaskId() const noexcept;
};
```

- **线程**：调度器非线程安全，仅限 GameThread；`Step` 内置重入防护。
- **预算**：`SetFrameBudgetMilliseconds` 低于 `0.05` 时钳制为 `0.05 ms`；`Step` 每完成一个 `StepWork` 检查预算，超出即让出至下一帧。
- **生命周期**：`StepWork` 返回 `false` 表示完成；宿主失效的任务自动摘除；`EnqueueBatch` 的 `TotalCount <= 0` 且宿主有效时立即执行 `OnComplete` 并返回空句柄。
- **空闲开销**：`UTimeSlicedTaskSubsystem::IsTickable()` 仅在存在待执行任务且非 CDO 时为 `true`，空队列零 Tick。
- **失败语义**：调度器 `EnqueueTask` 返回 `0` 表示入队失败；子系统返回的句柄 `IsActive() == false` 表示空句柄或任务已结束。

```cpp
UTimeSlicedTaskSubsystem& Subsystem = UTimeSlicedTaskSubsystem::Get(GetWorld());

FTimeSlicedTaskHandle Handle = Subsystem.EnqueueBatch(
    this,
    1000,
    [this](int32 ItemIndex) { ProcessEntity(ItemIndex); },
    [this]() { OnBatchFinished(); }
);
```

`SetClockSource` 可注入确定性时钟，供回放或纯 C++ 单测脱离墙钟。

## 分级节流心跳

### `FThrottledTickScheduler` / `UThrottledTickerSubsystem`

头文件：`#include "Async/FThrottledTickScheduler.h"`、`#include "Async/FThrottledTickTypes.h"`、`#include "Async/UThrottledTickerSubsystem.h"`

| 枚举值 | 数值 | 间隔 |
| :--- | :---: | :--- |
| `EThrottledRate::EveryFrame` | `0` | `0.0 s`（每帧） |
| `EThrottledRate::Rate30Hz` | `1` | `1/30 s` |
| `EThrottledRate::Rate10Hz` | `2` | `0.1 s` |
| `EThrottledRate::Rate2Hz` | `3` | `0.5 s` |
| `EThrottledRate::Rate1Hz` | `4` | `1.0 s` |

```cpp
class GAMECORE_API FThrottledTickScheduler {
public:
    using FThrottledTickCallback = TFunction<void(float DeltaTime)>;
    using FOwnerValidator = TFunction<bool()>;

    void Tick(float DeltaTime);
    uint64 RegisterIntervalTick(float IntervalSeconds, FThrottledTickCallback Callback);
    uint64 RegisterIntervalTick(FOwnerValidator InValidator, float IntervalSeconds, FThrottledTickCallback Callback);
    uint64 RegisterIntervalTick(TWeakObjectPtr<UObject> Owner, float IntervalSeconds, FThrottledTickCallback Callback);
    bool UnregisterTick(uint64 HandleId);
    bool SetTickInterval(uint64 HandleId, float NewIntervalSeconds);
    [[nodiscard]] bool IsTickActive(uint64 HandleId) const;
    [[nodiscard]] int32 GetRegisteredCount() const;
    void ClearAll();
    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] static float RateToIntervalSeconds(EThrottledRate Rate) noexcept;
};
```

```cpp
UCLASS()
class GAMECORE_API UThrottledTickerSubsystem : public UTickableWorldSubsystem {
public:
    FThrottledTickHandle RegisterTick(
        TWeakObjectPtr<UObject> Owner,
        EThrottledRate Rate,
        FThrottledTickCallback Callback
    );
    FThrottledTickHandle RegisterIntervalTick(
        TWeakObjectPtr<UObject> Owner,
        float IntervalSeconds,
        FThrottledTickCallback Callback
    );
    [[nodiscard]] int32 GetRegisteredCount() const;
    void ClearAll();
    [[nodiscard]] static UThrottledTickerSubsystem& Get(const UObject* WorldContextObject);
    [[nodiscard]] static UThrottledTickerSubsystem* GetPtr(const UObject* WorldContextObject);
};

class GAMECORE_API FThrottledTickHandle {
public:
    bool Unregister();
    bool SetRate(EThrottledRate NewRate);
    bool SetInterval(float NewIntervalSeconds);
    [[nodiscard]] bool IsActive() const;
    [[nodiscard]] uint64 GetHandleId() const noexcept;
};
```

- **线程**：调度器与子系统 Tick 仅限 GameThread。
- **相位交错**：注册时按 `AllocatedId % 8 * (Interval / 8)` 注入初相位，将同周期心跳打散到不同帧。
- **生命周期**：宿主弱引用失效后回调被拦截并在 Tick 结束时批量剔除；`FThrottledTickHandle` 可安全拷贝，`Unregister()` 幂等。
- **失败语义**：`IntervalSeconds <= 0` 视为每帧执行；调度器 `RegisterIntervalTick` 返回 `0` 表示注册失败；`UnregisterTick` / `SetTickInterval` 对未知句柄返回 `false`。
- **空闲开销**：无活跃注册项时 `IsTickable()` 返回 `false`。

```cpp
UThrottledTickerSubsystem& Ticker = UThrottledTickerSubsystem::Get(this);

FThrottledTickHandle Handle = Ticker.RegisterTick(
    this,
    EThrottledRate::Rate10Hz,
    [this](float DeltaTime) { RefreshNearbyTargets(); }
);

Handle.SetRate(EThrottledRate::Rate2Hz);
```

## C++20 协程

### `TGameCoroTask<T>` / `Game::Coro` 挂起原语

头文件：`#include "Async/TGameCoroTask.h"`、`#include "Async/FGameCoroAwaiters.h"`

```cpp
template <typename T = void>
class TGameCoroTask {
public:
    [[nodiscard]] EGameCoroStatus GetStatus() const noexcept;
    [[nodiscard]] bool IsCompleted() const noexcept;
    [[nodiscard]] bool IsCancelled() const noexcept;
    [[nodiscard]] bool IsDone() const noexcept;
    bool Cancel() noexcept;
    void BindContext(UObject* InOwner, FCancellationToken InToken = FCancellationToken::None()) noexcept;
    [[nodiscard]] TGameStatusResult<T> GetResult() const;
    void WhenCompleted(TFunction<void(TGameStatusResult<T>)> InCallback) const;
};
```

```cpp
namespace Game::Coro {

[[nodiscard]] inline FGameCoroContextBinding BindContext(
    UObject* InOwner,
    FCancellationToken InToken = FCancellationToken::None()
) noexcept;
[[nodiscard]] inline FGameCoroContextBinding BindCancellationToken(FCancellationToken InToken) noexcept;

[[nodiscard]] inline FSwitchToBackgroundAwaiter SwitchToBackground() noexcept;
[[nodiscard]] inline FSwitchToGameThreadAwaiter SwitchToGameThread() noexcept;
template <typename TResult>
[[nodiscard]] TRunOnBackgroundAwaiter<TResult> RunOnBackground(TFunction<TResult()> InWork);

[[nodiscard]] GAMECORE_API FNextFrameAwaiter NextFrame(const UObject* WorldContextObject) noexcept;
[[nodiscard]] GAMECORE_API FWaitSecondsAwaiter WaitSeconds(const UObject* WorldContextObject, float InDurationSeconds) noexcept;
[[nodiscard]] GAMECORE_API FWaitUntilAwaiter WaitUntil(
    const UObject* WorldContextObject,
    float InPollIntervalSeconds,
    TFunction<bool()> InPredicate
);
[[nodiscard]] GAMECORE_API FYieldTimeSliceAwaiter YieldTimeSlice(const UObject* WorldContextObject) noexcept;

}
```

| 等待原语 | 语义 | 调度内核 |
| :--- | :--- | :--- |
| `BindContext(Owner, Token)` | 绑定宿主弱引用与取消令牌；失效则挂起点自毁 | 立即校验 |
| `NextFrame(WorldContext)` | 等待下一帧 Tick 恢复 | `FThrottledTickScheduler` |
| `WaitSeconds(WorldContext, Sec)` | 等待指定秒数 | `FThrottledTickScheduler` |
| `WaitUntil(WorldContext, Interval, Pred)` | 按固定间隔轮询直到条件为真 | `FThrottledTickScheduler` |
| `YieldTimeSlice(WorldContext)` | 未超帧预算当帧继续，超出则跨帧恢复 | `FTimeSliceScheduler` |
| `SwitchToBackground()` | 后续代码切至工作线程 | `FGameThreadTaskRunner` |
| `SwitchToGameThread()` | 后续代码切回 GameThread（已在主线程则直通） | `FGameThreadTaskRunner` |
| `RunOnBackground<T>(WorkFn)` | 工作线程计算并自动切回发起线程交付结果 | `FGameThreadTaskRunner` |

- **线程**：挂起原语均实现 `SuspendWithGuard` 协议；`SwitchToGameThread` 在 GameThread 上零开销直通，跨线程恢复保证落在目标线程。
- **生命周期**：宿主销毁或取消时，挂起点守卫在恢复前二次校验并原地 `destroy()` 协程帧，不执行悬垂续体。
- **失败语义**：`GetResult()` 在 `Completed` 返回 `Success`；在 `Cancelled` 或宿主失效时返回 `Cancelled (6)`；仍在运行时返回 `InvalidState (7)`。
- **取消时机**：`Cancel()` 在下一个挂起点恢复前拦截；协程首行未执行前需要保护时，应在函数首行 `co_await Game::Coro::BindContext(...)`。

```cpp
TGameCoroTask<int32> UMyManager::ProcessAsyncData(FCancellationToken InToken)
{
    co_await Game::Coro::BindContext(this, InToken);

    const int32 HeavyResult = co_await Game::Coro::RunOnBackground<int32>([]() {
        return ComputeComplexAlgorithm();
    });

    co_await Game::Coro::NextFrame(this);

    for (int32 Index = 0; Index < HeavyResult; ++Index)
    {
        ApplyResultStep(Index);
        co_await Game::Coro::YieldTimeSlice(this);
    }

    co_return HeavyResult;
}
```

## 常见错误与边界

- **工作线程触碰 UObject**：`Run` / `RunOnBackground` 的工作体不得访问 `UObject`、GC 或引擎单例；所有对象交互必须放在 `RunOnGameThread` 续体或切回主线程之后。
- **取消不中断执行**：`Cancel()` 与取消令牌都是协作式的，仅阻止尚未开始的工作体；执行中的工作体需自行轮询 `IsCancellationRequested()` 退出。
- **空任务**：空的 `BackgroundWork` 或空的 `FStepTaskCallback` 不会派发；调度器入队失败返回 `0`。
- **宿主失效**：`Owner` 失效时后台任务不启动、主线程续体静默丢弃、时间切片与节流任务自动摘除，均不产生回调。
- **子系统获取**：`Get()` 在上下文无效时触发断言；可能为空的路径使用 `GetPtr()`。
- **协程结果**：协程未完成时调用 `GetResult()` 返回 `InvalidState (7)`，不要将其当作成功值使用。

## 测试与回归

<details>
<summary>展开 Async 自动化用例分组</summary>

- `Game.Core.Async.CancellationToken.*`（4 条）
- `Game.Core.Async.Coro.*`（8 条）
- `Game.Core.Async.CancelableScopeGuard.*`（4 条）
- `Game.Core.Async.Tasks.TaskRunner.*`（4 条）
- `Game.Core.Async.Tasks.TimeSliceScheduler.*`（1 条）
- `Game.Core.Async.Tasks.ThrottledTickScheduler.*`（2 条）
- `Game.Core.Async.Tasks.TimeSliced.*`（2 条）
- `Game.Core.Async.Tasks.ThrottledTicker.*`（2 条）

</details>

```bash
python3 .scripts/run_automation_tests.py --case Game.Core.Async
```

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [性能基准](../Architecture/Performance.md)
- [快速上手](../Guides/QuickStart.md)
