# GameCore 异步任务调度与并发协程 API 参考

本文档是 `Plugins/GameCore` 异步调度层（Async）的唯一权威规范（SSOT），系统涵盖协同取消、后台任务投递、帧预算时间切片、分级节流心跳与 C++20 生产级无栈协程。

---

## 一、 架构总览与设计原则

```
+-----------------------------------------------------------------------------------------+
|                                    业务调用层与协程交互                                    |
|   co_await Game::Coro::WaitSeconds / YieldTimeSlice / RunOnBackground / NextFrame       |
+-----------------------------------------------------------------------------------------+
                                          |
                +-------------------------+-------------------------+
                |                                                   |
                v                                                   v
+-------------------------------+                   +-------------------------------+
|     UTimeSlicedTaskSubsystem  |                   |   UThrottledTickerSubsystem   |
| (UTickableWorldSubsystem 薄壳) |                   | (UTickableWorldSubsystem 薄壳) |
+-------------------------------+                   +-------------------------------+
                |                                                   |
                v                                                   v
+-------------------------------+                   +-------------------------------+
|      FTimeSliceScheduler      |                   |    FThrottledTickScheduler    |
|   (纯 C++ 帧预算调度算法内核)   |                   |  (纯 C++ 分相交错节流调度内核)  |
+-------------------------------+                   +-------------------------------+
                \                                                   /
                 +------------------------+------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------------+
|                FGameThreadTaskRunner (后台工作线程派发与 GameThread 续体回跳)               |
|            + FCancellationToken / FCancellationTokenSource (无锁轮询与级联取消)           |
+-----------------------------------------------------------------------------------------+
```

### 核心红线与设计原则
1. **工作线程 UObject 零接触**：后台任务运行于线程池工作线程，**严禁触碰任何 UObject、引擎 GC 状态或引擎单例**；所有 UObject 交互必须收敛在回跳主线程的续体中。
2. **机制与外壳彻底分离 (Thin-Shell Pattern)**：`FTimeSliceScheduler` 与 `FThrottledTickScheduler` 为纯 C++ 算法核心，零 `UWorld` 依赖，可在任何多线程、独立离线命令行工具或纯 C++ 单元测试中独立执行；`UTimeSlicedTaskSubsystem` 与 `UThrottledTickerSubsystem` 作为引擎外壳薄层，负责挂接 `UWorld` 生命周期并在队列为空时通过 `IsTickable() == false` 保持**零空闲 Tick 开销**。
3. **出锁派发与零锁原子轮询 (Out-of-Lock Dispatch)**：`FCancellationState` 与 `TGameCoroState` 的取消与终态派发严格遵守出锁派发铁律，绝不在临界区内调用未知业务闭包，彻底根除死锁；状态轮询采用 Acquire-Release 原子变量，实现无锁零开销查询。
4. **协程双重生命周期熔断 (Zero UAF & Zero Leak)**：`TGameCoroTask` 挂起点守卫 `FGameCoroSuspensionGuard` 在任何挂起点唤醒前强制二次校验宿主存活与取消状态；若失效或被取消，自动原地调用 `CoroHandle.destroy()` 触发协程帧局部 RAII 析构，绝不恢复执行挂起点之后的悬垂代码。

---

## 二、 模块 API 规格

### 1. `FCancellationToken` & `FCancellationTokenSource` (协同取消原语)

头文件：`#include "Async/FCancellationToken.h"`、`#include "Async/FCancellationTokenSource.h"`

#### 核心类型与职责
- `FCancellationToken`：纯值语义轻量取消令牌（无锁原子轮询 `IsCancellationRequested()`，可任意廉价拷贝）。
- `FCancellationTokenSource`：取消信号发生源，负责调用 `Cancel()` 广播取消并签发只读令牌。
- `FCancellationTokenRegistration`：RAII 取消回调注销句柄，析构时自动解绑，防止悬垂回调。

#### 级联联合取消 (Linked Token Source)
当复合异步操作依赖多个取消触发点时（例如：用户取消、超时取消、宿主销毁），使用 `CreateLinkedTokenSource` 创建联合源，任一输入令牌触发即级联取消衍生源：

```cpp
FCancellationTokenSource UserCancelSource;
FCancellationTokenSource TimeoutCancelSource;

// 创建联合源：任一源取消，LinkedSource 自动触发取消
TSharedRef<FCancellationTokenSource> LinkedSource = 
    FCancellationTokenSource::CreateLinkedTokenSource(UserCancelSource.GetToken(), TimeoutCancelSource.GetToken());

FCancellationToken CombinedToken = LinkedSource->GetToken();
```

---

### 2. `FGameThreadTaskRunner` (统一异步执行工具)

头文件：`#include "Async/FGameThreadTaskRunner.h"`

#### 静态接口
```cpp
class GAMECORE_API FGameThreadTaskRunner {
public:
    FGameThreadTaskRunner() = delete;

    /**
     * @brief 派发后台工作线程任务（自动追踪在飞任务计数）
     */
    static FGameAsyncTaskHandle Run(TWeakObjectPtr<UObject> Owner, TFunction<void()> BackgroundWork);
    static FGameAsyncTaskHandle Run(TWeakObjectPtr<UObject> Owner, FCancellationToken CancellationToken, TFunction<void()> BackgroundWork);
    static FGameAsyncTaskHandle Run(FCancellationToken CancellationToken, TFunction<void()> BackgroundWork);

    /**
     * @brief 回到 GameThread 主线程执行续体（宿主失效时安全丢弃）
     */
    static void RunOnGameThread(TWeakObjectPtr<UObject> Owner, TFunction<void()> GameThreadWork);
    static void RunOnGameThread(TFunction<void()> GameThreadWork);

    /**
     * @brief 协同等待所有在飞后台任务排空（供模块关停或单元测试使用）
     */
    static bool FlushInFlightTasks(double TimeoutSeconds = 2.0);
};
```

---

### 3. `FTimeSliceScheduler` & `UTimeSlicedTaskSubsystem` (帧预算时间切片)

头文件：`#include "Async/FTimeSliceScheduler.h"`、`#include "Async/UTimeSlicedTaskSubsystem.h"`

#### 算法机制
- **协作式步进与超时截止**：每完成一个子步骤，检查高精度时钟；若超出 `FrameBudgetSeconds`（默认 1.5ms，最低钳制 0.05ms），主动让出主线程，将剩余步骤推迟至下一帧。
- **O(1) 紧凑出队**：内部通过 `QueueHeadIndex` 避免每次 `RemoveAt(0)` 的搬移开销；当队头累计闲置槽位达到阈值时批量压缩。
- **时钟注入**：支持通过 `SetClockSource` 注入确定性时钟源，供回放或纯 C++ 单测脱离墙钟运行。

#### 使用示例
```cpp
UTimeSlicedTaskSubsystem& Subsystem = UTimeSlicedTaskSubsystem::Get(GetWorld());

// 分帧批处理 1000 个实体，平摊主线程峰值防掉帧
FTimeSlicedTaskHandle Handle = Subsystem.EnqueueBatch(
    this,
    1000,
    [this](int32 ItemIndex) {
        this->ProcessEntity(ItemIndex);
    },
    [this]() {
        this->OnBatchFinished();
    }
);
```

---

### 4. `FThrottledTickScheduler` & `UThrottledTickerSubsystem` (分级交错节流心跳)

头文件：`#include "Async/FThrottledTickScheduler.h"`、`#include "Async/UThrottledTickerSubsystem.h"`

#### 核心特性
- **分相交错削峰 (Phase Interleaving)**：注册时按 `AllocatedId % 8ULL * (Interval / 8.0f)` 注入相位偏移，将相同周期的心跳对象均匀打散至不同帧执行，杜绝周期性集中掉帧。
- **宿主弱引用守卫**：持有 `TWeakObjectPtr<UObject>`，宿主销毁时自动拦截回调，Tick 结束后批量安全剔除。
- **频率档位与动态调频**：
  - `EThrottledRate::EveryFrame` (全速)
  - `EThrottledRate::Rate30Hz` (~0.033s)
  - `EThrottledRate::Rate10Hz` (~0.1s)
  - `EThrottledRate::Rate2Hz` (~0.5s)
  - `EThrottledRate::Rate1Hz` (~1.0s)
  - 支持 `Handle.SetRate()` 与 `Handle.SetInterval()` 随时动态调频。

---

### 5. `TGameCoroTask<T>` & `FGameCoroAwaiters` (C++20 生产级无栈协程系统)

头文件：`#include "Async/TGameCoroTask.h"`、`#include "Async/FGameCoroAwaiters.h"`

#### 挂起点安全架构
所有挂起原语均满足 `SuspendWithGuard(TSharedRef<FGameCoroSuspensionGuard>)` 协议。`FGameCoroSuspensionGuard` 在唤醒协程时执行：
```cpp
bool ResumeOrAbort() {
    if (bHandled.exchange(true, std::memory_order_acq_rel)) return false;
    if (State->ShouldAbort()) {
        State->MarkCancelled();
        CoroHandle.destroy(); // 原地销毁协程帧，触发局部变量 RAII
        return false;
    }
    CoroHandle.resume();
    return true;
}
```

#### 常用 Awaiter 规格表
| 等待原语 | 语义说明 | 调度内核 |
|---|---|---|
| `co_await Game::Coro::BindContext(Owner, Token)` | 绑定宿主 UObject 弱引用与取消令牌 | 立即校验（失效则原地自毁） |
| `co_await Game::Coro::NextFrame(WorldContext)` | 挂起并等待至下一帧主线程 Tick 开始恢复 | `FThrottledTickScheduler` |
| `co_await Game::Coro::WaitSeconds(WorldContext, Sec)` | 挂起指定秒数后由节流调度器恢复 | `FThrottledTickScheduler` |
| `co_await Game::Coro::WaitUntil(WorldContext, Interval, Pred)` | 按固定间隔低频轮询条件直至为真 | `FThrottledTickScheduler` |
| `co_await Game::Coro::YieldTimeSlice(WorldContext)` | 时间切片出让（未超预算同帧继续，超预算跨帧） | `FTimeSliceScheduler` |
| `co_await Game::Coro::SwitchToBackground()` | 将后续代码切往工作线程执行 | `FGameThreadTaskRunner` |
| `co_await Game::Coro::SwitchToGameThread()` | 将后续代码切回 GameThread 主线程执行 | `FGameThreadTaskRunner` |
| `co_await Game::Coro::RunOnBackground<T>(WorkFn)` | 工作线程计算并在完成后自动携结果切回原线程 | `FGameThreadTaskRunner` |

#### 完整使用示例
```cpp
TGameCoroTask<int32> UMyManager::ProcessAsyncData(FCancellationToken InToken)
{
    // 1. 绑定宿主与取消令牌（后续任何挂起点若宿主销毁自动自毁协程帧）
    co_await Game::Coro::BindContext(this, InToken);

    // 2. 切到后台线程计算重型算法
    const int32 HeavyResult = co_await Game::Coro::RunOnBackground<int32>([]() {
        return ComputeComplexAlgorithm();
    });

    // 3. 等待下一帧主线程 Tick
    co_await Game::Coro::NextFrame(this);

    // 4. 分帧切片迭代
    for (int32 Index = 0; Index < HeavyResult; ++Index) {
        this->ApplyResultStep(Index);
        co_await Game::Coro::YieldTimeSlice(this);
    }

    co_return HeavyResult;
}
```

---

## 三、 自动化测试套件

Async 组件全部测试用例统一定义于 `Game.Core.Async.*`：
- `Game.Core.Async.CancellationToken.*` (4 条)
- `Game.Core.Async.Coro.*` (5 条)
- `Game.Core.Async.CancelableScopeGuard.*` (3 条)
- `Game.Core.Async.Tasks.TaskRunner.*` (3 条)
- `Game.Core.Async.Tasks.TimeSliceScheduler.*` (1 条)
- `Game.Core.Async.Tasks.ThrottledTickScheduler.*` (1 条)
- `Game.Core.Async.Tasks.TimeSliced.*` (2 条)
- `Game.Core.Async.Tasks.ThrottledTicker.*` (2 条)

单指令一键全量回归：
```bash
python3 .scripts/run_automation_tests.py --case Game.Core.Async
```
