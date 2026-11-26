# GameFlow 公共 API 契约参考手册

## 1. 核心类型与委托 (`Public/Core/GameFlowTypes.h`)

| 符号 | 类型 | 说明 |
|---|---|---|
| `FLoadingBarrierHandle` | `TGameHandle<FLoadingBarrierTag>` | 64 位强类型代际句柄（高 32 位代际号 + 低 32 位槽位索引），防过期复用 |
| `ELoadingTransitionPhase` | `UENUM(BlueprintType)` | 加载阶段：`Idle`、`LoadingMap`、`WaitingBarriers` |
| `FLoadingBarrierTelemetry` | `USTRUCT(BlueprintType)` | 单屏障耗时明细：`BarrierName`、`DurationMs`、`Weight`、`bTimedOut` |
| `FLoadingTelemetryReport` | `USTRUCT(BlueprintType)` | 单次关卡流转总报告：`LevelName`、`TotalDurationMs`、`MapLoadDurationMs`、`bTimedOut`、`Barriers` |
| `FOnLoadingStateChanged` / `Native` | 动态 / 原生多播 | `(bool bIsLoading, FName TargetLevelName)` 仅在真实开始与结束加载时触发 |
| `FOnLoadingProgressChanged` / `Native` | 动态 / 原生多播 | `(float Progress, const FText& StepDescription)` 加权综合进度 `[0, 1]` |
| `FOnLoadingTelemetryReport` / `Native` | 动态 / 原生多播 | `(const FLoadingTelemetryReport& Report)` 每次流转结束派发完整遥测 |
| `FOnLoadingAbortedNative` | 原生多播 | 仅在活跃加载期子系统 `Deinitialize()` 被迫中止时触发 |

---

## 2. 关卡加载与屏障子系统 (`Public/System/ULoadingSubsystem.h`)

### 2.1 获取与跳转
- `static ULoadingSubsystem* Get(const UObject* WorldContextObject)`：支持 `ULoadingSubsystem*`、`UGameInstance*`、`UGameInstance` Outer 链对象、`UWorld*` 与任意有效 WorldContext；无效或已标记垃圾的对象返回 `nullptr`。
- `void OpenLevelWithLoading(const FName& LevelName)`：开启新一轮加载流转并调用 `UGameplayStatics::OpenLevel`；加载中重复调用或 `NAME_None` 将告警忽略；在加载结束收尾广播期间重入调用将自动排队至本轮全部广播完成后顺序发起下一轮跳转（收尾期间二次排队将告警忽略）。

### 2.2 C++ 与蓝图屏障管理
- `[[nodiscard]] FLoadingBarrierToken AcquireBarrierToken(const FName& BarrierName, float InWeight = 1.0f, float InTimeoutSeconds = 0.0f)`：零堆分配栈上值语义 RAII 令牌。权重下限 `0.1f`（非有限回落 `1.0f`），单屏障超时 `>0` 时即使全局看门狗关闭亦独立生效。
- `int64 AcquireLoadingBarrierBp(FName BarrierName, float InWeight = 1.0f, float InTimeoutSeconds = 0.0f)`：蓝图申请屏障并返回非零 64 位代际打包句柄。
- `bool ReleaseLoadingBarrierBp(int64 BarrierId)`：蓝图释放屏障，代际防重放（无效或重复释放返回 `false`）。
- `bool ReportBarrierProgressBp(int64 BarrierId, float Progress, const FText& Description)`：蓝图上报进度（自动 clamp 至 `[0, 1]`，非有限值拒绝并返回 `false`）。

### 2.3 状态查询与看门狗
- `bool IsLoading() const noexcept` / `ELoadingTransitionPhase GetLoadingPhase() const noexcept`
- `FName GetTargetLevelName() const noexcept` / `float GetLoadingProgress() const noexcept`
- `bool HasActiveBarriers() const noexcept` / `int32 GetActiveBarrierCount() const noexcept`
- `const FLoadingTelemetryReport& GetLastTelemetryReport() const noexcept`
- `void SetMaxLoadingTimeoutSeconds(float InTimeoutSeconds)` / `float GetMaxLoadingTimeoutSeconds() const noexcept`（实时联动看门狗定时器启停；`<=0` 或非有限值关闭全局超时）
- `void CheckWatchdogTimeout()` / `void CheckWatchdogTimeoutAt(double CurrentTimeSeconds)`（纯 C++ 巡检入口与确定性时钟测试入口）

---

## 3. 屏障 RAII 令牌 (`Public/System/FLoadingBarrierToken.h`)

- 不可拷贝、仅支持移动语义（移动赋值先释放当前持有的旧屏障，自移动安全，源对象重置为空状态）。
- `void Release()`：幂等显式释放屏障。
- `void ReportProgress(float InProgress, const FText& InDescription = FText::GetEmpty())`：上报进度并推动全局进度广播；非有限浮点或屏障已失效时静默忽略。
- `bool IsReleased() const noexcept`：查询本地令牌是否已释放/被移动。
- `bool IsValid() const noexcept` / `explicit operator bool() const noexcept`：实时查询子系统槽位表确认屏障仍在活跃有效态（看门狗强制超时后立即返回 `false`）。
- `float GetProgress() const noexcept` / `FName GetBarrierName() const noexcept` / `FLoadingBarrierHandle GetBarrierHandle() const noexcept`。

---

## 4. C++20 协程等待原语 (`Public/Async/FWaitLoadingCompleteAwaiter.h`)

```cpp
[[nodiscard]] GAMEFLOW_API FWaitLoadingCompleteAwaiter WaitLoadingComplete(
    const UObject* WorldContextObject,
    FCancellationToken CancellationToken = FCancellationToken::None(),
    float TimeoutSeconds = 0.0f
);
```

- **返回值语义**：
  - `true`：当前非加载态（零堆分配直通），或加载与全部屏障正常完成；
  - `false`：上下文无效、令牌已预取消、挂起期同/跨线程取消、等待超时或加载子系统销毁中止。
- **并发与生命周期保证**：后台线程触发 `CancellationToken` 自动通过 `FGameThreadTaskRunner::RunOnGameThread` 回投主线程恢复协程；调用 `TGameCoroTask::Cancel()` 或绑定宿主销毁时立即释放所有委托与定时器零泄漏。

---

## 5. 蓝图异步等待动作节点 (`Public/Blueprint/AsyncAction_WaitForLoadingComplete.h`)

- 工厂节点：`WaitForLoadingComplete(const UObject* WorldContextObject)`
- 执行引脚：
  - `OnCompleted`：当前非加载态或关卡加载与全部屏障解除完成后触发；
  - `OnCancelled`：上下文无效、主动调用 `Cancel()` 或子系统销毁中止时触发。
- 生命周期契约：`Activate()` 幂等、广播前先解绑委托防重入、`BeginDestroy()` 兜底清理、直连 `Subsystem->GetGameInstance()` 完成 GC 注册保活。
