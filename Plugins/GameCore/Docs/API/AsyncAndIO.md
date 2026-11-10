# GameCore 异步任务调度与节流 API 参考

本文档汇总 `GameCore` 核心调度层提供的基础异步与心跳执行设施：
1. `FGameThreadTaskRunner`：后台工作线程任务与回主线程安全续体
2. `UTimeSlicedTaskSubsystem`：主线程帧预算时间切片调度器（平摊重型主线程批处理防掉帧）
3. `UThrottledTickerSubsystem`：分级交错节流心跳子系统（削峰平摊高频心跳，零空闲开销）

> **组件解耦与迁移提示**：
> - **资产异步加载与内存保活**（`FGameAssetLoader` / `FScopedPinnedAsset`）：已迁移至独立插件 `Plugins/GameAssetCore`。
> - **关卡流转与就绪屏障**（`ULoadingSubsystem` / `FLoadingBarrierToken`）：已迁移至业务流插件 `Plugins/GameFlow`。
> - **目录变更热重载监听**（`FDirectoryChangeWatcher`）：已归位至数据插件 `Plugins/GameData` (`GameDataCore/Utils`)。

---

## 1. `FGameThreadTaskRunner` (统一异步执行工具)

头文件：`#include "Async/FGameThreadTaskRunner.h"`

### 设计规范与红线
- 后台任务运行于工作线程（Work Thread），**严禁触碰任何 UObject、引擎 GC 状态或引擎单例**；
- 一切对 UObject 的读写操作必须放在 `GameThread` 续体内；
- 续体调度前强制校验 `TWeakObjectPtr<UObject> Owner` 的有效性。若宿主对象已提前销毁，续体直接丢弃不执行，彻底消灭野指针与生命周期竞态。

### 静态接口

```cpp
class GAMECORE_API FGameThreadTaskRunner {
public:
    FGameThreadTaskRunner() = delete;

    /**
     * @brief 派发后台工作线程任务（绑定宿主弱引用与可选协同取消令牌）
     */
    static FGameAsyncTaskHandle Run(TWeakObjectPtr<UObject> Owner, TFunction<void()> BackgroundWork);
    static FGameAsyncTaskHandle Run(TWeakObjectPtr<UObject> Owner, FCancellationToken CancellationToken, TFunction<void()> BackgroundWork);

    /**
     * @brief 派发无特定 UObject 宿主绑定的全局后台工作线程任务
     */
    static FGameAsyncTaskHandle Run(FCancellationToken CancellationToken, TFunction<void()> BackgroundWork);

    /**
     * @brief 回到 GameThread 主线程执行续体（宿主失效时安全丢弃）
     */
    static void RunOnGameThread(TWeakObjectPtr<UObject> Owner, TFunction<void()> GameThreadWork);
    static void RunOnGameThread(TFunction<void()> GameThreadWork);
};
```

---

## 2. `UTimeSlicedTaskSubsystem` (主线程帧预算时间切片执行器)

头文件：
- `#include "Async/UTimeSlicedTaskSubsystem.h"`
- `#include "Async/FTimeSlicedTaskHandle.h"`

基类：`UTickableWorldSubsystem`（当任务队列为空时 `IsTickable()` 返回 `false`，保持零空闲 Tick 损耗）

- `SetFrameBudgetMilliseconds(double BudgetMs)`：配置单帧最大执行预算（默认 `1.5ms`）。
- `EnqueueTask(Owner, StepWork)` / `EnqueueTask(StepWork)`：步进闭包返回 `true` 代表尚有后续工作留待下一帧，返回 `false` 代表完成。宿主 `Owner` 销毁时自动摘除跳过。
- `EnqueueBatch(Owner, TotalCount, PerItemWork, OnComplete)`：定长数组分帧批处理并在全部完成后回调 `OnComplete`。
- 返回 `FTimeSlicedTaskHandle`，支持随时调用 `Cancel()`（即使在步进回调内部自取消亦安全）。

---

## 3. `UThrottledTickerSubsystem` (分级交错节流心跳子系统)

头文件：
- `#include "Async/UThrottledTickerSubsystem.h"`
- `#include "Async/FThrottledTickTypes.h"`

### 职责与原理
- 解决海量纯 C++ 实体、AI 决策、未挂载 Component 的轻量对象每帧 Tick 开销过大的痛点；
- 支持预设频率档位（`EveryFrame`、`Rate30Hz`、`Rate10Hz`、`Rate2Hz`、`Rate1Hz`）或自定义秒级间隔；
- **分相交错（Interleaved Phase Staggering）**：注册时自动计算相位偏移，将相同频率的心跳对象均匀打散到不同帧执行，彻底削峰平摊帧耗时；
- **宿主弱引用守卫**：宿主对象销毁时自动短路并不解引用，Tick 结束后自动安全剔除；
- **零空闲开销**：当没有注册任务时，`IsTickable()` 返回 `false` 自动休眠；
- 返回 `FThrottledTickHandle`，支持随时 `Unregister()`、`SetRate()` 或 `SetInterval()` 动态调频。

```cpp
UThrottledTickerSubsystem& Ticker = UThrottledTickerSubsystem::Get(GetWorld());

// 注册 10Hz 节流心跳（自动分相平摊，宿主销毁自动解挂）
FThrottledTickHandle Handle = Ticker.RegisterTick(
    this,
    EThrottledRate::Rate10Hz,
    [this](float AccumulatedDeltaTime) {
        this->PerformLowFrequencyUpdate(AccumulatedDeltaTime);
    }
);

// 动态调频或主动解挂
Handle.SetRate(EThrottledRate::Rate2Hz);
Handle.Unregister();
```

---

## 4. `TGameCoroTask<T>` & `FGameCoroAwaiters` (C++20 轻量级生产协程原语集)

头文件：
- `#include "Async/TGameCoroTask.h"`
- `#include "Async/FGameCoroAwaiters.h"`

### 核心设计优势
- **零额外 Subsystem**：100% 复用 `FGameThreadTaskRunner`、`FThrottledTickScheduler`（`UThrottledTickerSubsystem`）与 `FTimeSliceScheduler`（`UTimeSlicedTaskSubsystem`）。
- **双重生命周期熔断（零 UAF、零帧泄漏）**：
  - 支持在协程体内通过 `co_await Game::Coro::BindContext(Owner, Token)` 绑定宿主 `UObject` 弱引用与 `FCancellationToken`。
  - 在任意挂起点恢复前，若宿主 `UObject` 已销毁、调用了 `Task.Cancel()`、触发了 `FCancellationToken` 或所属调度器执行了 `ClearAll()`，挂起点守卫 `FGameCoroSuspensionGuard` 会直接将任务标记为 `EGameCoroStatus::Cancelled` 并原地调用 `CoroHandle.destroy()` 触发协程局部 RAII 析构，**绝不恢复执行挂起点之后的任何悬垂业务代码**。
- **原生融合 `TGameResult<T>`**：外部通过 `Task.GetResult()` 或父协程 `co_await SubTask` 直接取得 `TGameResult<T>`（取消时返回 `Game::Error::Cancelled`）。

### 经典使用示例

```cpp
TGameCoroTask<int32> UMyGameplaySystem::RunMultiStageFlow(FCancellationToken Token)
{
    // 1. 绑定宿主 UObject 与协同取消令牌（后续任意 co_await 若宿主销毁自动安全自毁协程帧）
    co_await Game::Coro::BindContext(this, Token);

    // 2. 在后台工作线程执行重型计算并自动带回 GameThread
    const int32 ComputedScore = co_await Game::Coro::RunOnBackground<int32>([]() {
        return PerformHeavyPathfindingOrDecompression();
    });

    // 3. 在主线程基于节流调度器等待 0.25 秒
    co_await Game::Coro::WaitSeconds(this, 0.25f);

    // 4. 分帧时间切片循环处理，超帧预算自动跨帧恢复
    for (int32 Index = 0; Index < 100; ++Index) {
        this->ProcessChunkItem(Index);
        co_await Game::Coro::YieldTimeSlice(this);
    }

    co_return ComputedScore;
}
```
