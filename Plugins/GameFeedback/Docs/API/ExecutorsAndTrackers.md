# GameFeedback 执行器与生命周期追踪器 API 参考

> **一句话**：闪白、镜头震动、顿帧限流与预测去重的追踪器，以及 VFX / 音频无状态执行器的内部 API。
> **头文件**：`Private/Tracker/*.h`、`Private/Execution/*.h`（插件私有，不对外导出） · **线程**：仅 GameThread；定时器回调随 `UWorld::GetTimerManager()` 在游戏线程执行 · **失败语义**：执行器返回 `false` / `nullptr` 表示本次未派发；追踪器对失效对象静默清理

## 30 秒速览

- **闪白** `FGameFeedbackFlashTracker`：绝对到期时间续期，弱存活令牌 + 析构兜底防 UAF。
- **震动** `FGameFeedbackShakeTracker`：按 `APlayerCameraManager` 隔离，分屏互不干扰，同帧只保留最大振幅。
- **限流** `FGameFeedbackHitStopLimiter`：攻击者 / 目标滑动窗口冷却 + 累积时长预算，纯逻辑无 World 依赖。
- **预测** `FGameFeedbackPredictionHistory`：`(PredictionEventId, Attacker, Target)` 三要素匹配，`0.5s` 窗口内有界去重。
- **执行器**：`GameFeedback::Execution` 将 VFX / 音频的决策计算与引擎副作用分离，支持无头测试与软引用去重预热。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `FGameFeedbackFlashTracker::Execute` | 应用材质受击闪白 | 仅 GameThread；同一网格按绝对到期时间续期 |
| `FGameFeedbackShakeTracker::Execute` | 在指定摄像机管理器启动震动 | 空 `CameraManager` / 空类不启动；同帧仅保留最大振幅 |
| `FGameFeedbackShakeTracker::StopAll` | 停止全部震动并清理记录 | 仅 GameThread |
| `FGameFeedbackHitStopLimiter::PassCooldown` | 判定是否越过 `(攻击者, 目标)` 冷却 | 双空实体返回 `true` 且不写冷却表 |
| `FGameFeedbackHitStopLimiter::AcquireDuration` | 取累积预算内可应用时长 | 预算耗尽或非法输入返回 `0` |
| `FGameFeedbackPredictionHistory::Add` | 登记预测事件指纹 | `PredictionEventId == 0` 不记录；上限 `128` 条 |
| `FGameFeedbackPredictionHistory::ConsumeMatch` | 消费匹配的预测事件 | 仅 `0.5s` 窗口内三要素全匹配返回 `true` |
| `GameFeedback::Execution::ResolveVFXSpawnPlan` | 纯逻辑推导 VFX 生成计划 | 支持 `-nullrhi`，可在无头测试直接调用 |
| `GameFeedback::Execution::SpawnVFX` | 生成 Niagara 特效 | 门禁拦截、DS、无头或预热中返回 `nullptr` |
| `GameFeedback::Execution::PlayImpactAudio` | 同步派发受击音效 | DS 或预热中返回 `false`；仅在 GameThread |

## 受击闪白 `FGameFeedbackFlashTracker`

```cpp
void Execute(UWorld* World, AActor* Target, const FHitResult& HitResult,
             const FGameFeedbackHitFlashAction& Action, float Magnitude);
void ClearActor(UWorld* World, AActor* Target);
void Shutdown(UWorld* World);
void PruneInvalid(UWorld* World = nullptr);
[[nodiscard]] int32 GetTrackedCount() const noexcept;
void SetTimeProvider(TFunction<double()> InTimeProvider);
```

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `Execute` | `World`、`Target`、`HitResult`、`Action`、`Magnitude` | `void` | 应用闪白；同一网格多次命中按绝对到期时间续期 |
| `ClearActor` | `World`、`Target` | `void` | 还原该 Actor 全部网格的原始 Overlay 并移除追踪 |
| `Shutdown` | `World` | `void` | 还原全部网格并清空追踪，子系统注销或析构时调用 |
| `PruneInvalid` | 可选 `World` | `void` | 清理失效网格记录并同步取消残留定时器 |
| `GetTrackedCount` | 无 | `int32` | 当前追踪记录数 |
| `SetTimeProvider` | `InTimeProvider` | `void` | 注入可控时钟，供测试使用 |

- **线程**：仅 GameThread；到期回调由世界定时器在游戏线程触发。
- **生命周期**：`FGameFeedbackFlashTracker` 不可拷贝、不可移动；析构自动兜底 `Shutdown()`，定时器绑定 `TWeakPtr<bool>` 存活令牌，宿主销毁后回调安全失效。
- **失败语义**：目标或网格无效时静默跳过；原始材质由 `TStrongObjectPtr` 保活，GC 后仍可安全还原。

```cpp
FGameFeedbackFlashTracker FlashTracker;
FlashTracker.Execute(GetWorld(), HitActor, HitResult, Definition->HitFlash, Signal.Magnitude);
```

## 镜头震动 `FGameFeedbackShakeTracker`

```cpp
void Execute(
    APlayerCameraManager* CameraManager,
    TSubclassOf<UCameraShakeBase> ShakeClass,
    float ShakeScale,
    const FVector& LocalImpactDirection,
    bool bOrientToImpact);
void StopAll();
[[nodiscard]] int32 GetTrackedCount() const noexcept;
```

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `Execute` | `CameraManager`、`ShakeClass`、`ShakeScale`、`LocalImpactDirection`、`bOrientToImpact` | `void` | 启动或替换指定摄像机管理器上的震动 |
| `StopAll` | 无 | `void` | 停止全部已启动震动并清理记录 |
| `GetTrackedCount` | 无 | `int32` | 当前追踪的有效摄像机管理器数量 |

- **线程**：仅 GameThread，直接操作本地 `APlayerCameraManager`。
- **生命周期**：以 `TObjectKey<APlayerCameraManager>` 为键，失效对象经 `IsValid(ResolveObjectPtr())` 自动清理。
- **失败语义**：空 `CameraManager` 或空震动类不启动；同一帧内仅保留最高振幅，弱振幅直接抑制。

```cpp
FGameFeedbackShakeTracker ShakeTracker;
ShakeTracker.Execute(
    LocalPC->PlayerCameraManager, ShakeClass, 1.5f, LocalImpactDirection, true);
```

## 顿帧限流 `FGameFeedbackHitStopLimiter`

```cpp
bool PassCooldown(AActor* Attacker, AActor* Target,
                  const FGameFeedbackHitStopAction& Action, double RealTime);
float AcquireDuration(AActor* Actor, float RequestedDuration,
                      const FGameFeedbackHitStopAction& Action, double RealTime);
void ClearActor(AActor* Actor);
void Prune(double RealTime);
void PruneIfNeeded(double RealTime);
void Reset();
```

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `PassCooldown` | `Attacker`、`Target`、`Action`、`RealTime` | `bool` | 越过 `(攻击者, 目标)` 冷却窗口返回 `true`；双空实体直接放行 |
| `AcquireDuration` | `Actor`、`RequestedDuration`、`Action`、`RealTime` | `float` | 返回累积预算内实际可应用时长；预算耗尽或非法输入返回 `0` |
| `ClearActor` | `Actor` | `void` | 清除该 Actor 的累积状态与相关冷却记录 |
| `Prune` | `RealTime` | `void` | 清理失效弱引用与超期记录 |
| `PruneIfNeeded` | `RealTime` | `void` | 按 `1.0s` 间隔或条目数 `>= 64` 节流清理 |
| `Reset` | 无 | `void` | 重置全部累积状态与冷却记录 |

- **线程**：纯逻辑，无 World 依赖；调用方需保证在 GameThread 使用同一时钟。
- **生命周期**：`Tracks` 与 `CooldownMap` 均以 `TObjectKey<AActor>` 存储；`Prune` 对累积状态使用 `FMath::Max(5.0, ResetWindow)` 作为寿命下限，避免长重置窗口被提前清空。
- **失败语义**：`PassCooldown` 在双空实体时返回 `true` 且不写冷却表；`AcquireDuration` 对空 Actor 或 `RequestedDuration <= 0` 返回 `0`。

```cpp
FGameFeedbackHitStopLimiter Limiter;
if (Limiter.PassCooldown(Attacker, Target, Action, Now)) {
    const float Applied = Limiter.AcquireDuration(Target, Action.VictimDuration, Action, Now);
}
```

## 预测指纹 `FGameFeedbackPredictionHistory`

```cpp
static constexpr int32 MaxEntries = 128;

void Add(uint16 PredictionEventId, const TWeakObjectPtr<AActor>& Attacker,
         const TWeakObjectPtr<AActor>& Target, double RealTime);
bool ConsumeMatch(uint16 PredictionEventId, const TWeakObjectPtr<AActor>& Attacker,
                  const TWeakObjectPtr<AActor>& Target, double RealTime);
void Prune(double RealTime);
[[nodiscard]] int32 GetEntryCount() const noexcept;
```

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `Add` | `PredictionEventId`、`Attacker`、`Target`、`RealTime` | `void` | `0` 不记录；满 `128` 条时淘汰最旧时间戳条目 |
| `ConsumeMatch` | `PredictionEventId`、`Attacker`、`Target`、`RealTime` | `bool` | 三要素完全匹配且未超 `0.5s` 窗口时消费并返回 `true` |
| `Prune` | `RealTime` | `void` | 用 `RemoveAllSwap` 清理超期或实体失效的条目 |
| `GetEntryCount` | 无 | `int32` | 当前缓存条目数 |

- **线程**：纯逻辑，无 World 依赖；仅在 GameThread 记录与消费。
- **生命周期**：底层为 `TArray<FEntry, TInlineAllocator<16>>`，容量上限 `128`，不会无界增长。
- **失败语义**：`PredictionEventId == 0` 永不记录、永不匹配；弱引用失效或超过 `0.5s` 的记录在下次访问时被清理。

```cpp
FGameFeedbackPredictionHistory History;
History.Add(Signal.PredictionEventId, Signal.Instigator, Signal.Target, Now);

const bool bSuppressed = History.ConsumeMatch(
    Event.PredictionEventId, Event.Instigator, Event.Target, Now);
```

## 无状态执行器 `GameFeedback::Execution`

### 粒子执行器

```cpp
enum class EGameFeedbackVFXSpawnMode : uint8 {
    Skipped, AsyncPreload, SpawnAtLocation, SpawnAttached
};

struct FGameFeedbackVFXSpawnPlan {
    EGameFeedbackVFXSpawnMode Mode = EGameFeedbackVFXSpawnMode::Skipped;
    UNiagaraSystem* System = nullptr;
    UPrimitiveComponent* AttachComponent = nullptr;
    FName BoneName = NAME_None;
    FVector Location = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
};

[[nodiscard]] FRotator ResolveVFXRotation(
    const FGameFeedbackSignal& Signal, EGameFeedbackVFXOrientation OrientationMode);

[[nodiscard]] FGameFeedbackVFXSpawnPlan ResolveVFXSpawnPlan(
    UWorld* World, const FGameFeedbackSignal& Signal, const FGameFeedbackVFXAction& Action);

UNiagaraComponent* SpawnVFX(
    UWorld* World, const FGameFeedbackSignal& Signal, const FGameFeedbackVFXAction& Action);
```

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `ResolveVFXRotation` | `Signal`、`OrientationMode` | `FRotator` | 反射法线先归一化，缺失轨迹向量时降级为法线朝向 |
| `ResolveVFXSpawnPlan` | `World`、`Signal`、`Action` | 执行计划 | 纯逻辑推导门禁、预热、附着与位姿，支持 `-nullrhi` |
| `SpawnVFX` | `World`、`Signal`、`Action` | `UNiagaraComponent*` | 生成成功返回组件；门禁拦截、DS、无头或预热中返回 `nullptr` |

### 音频执行器

```cpp
bool PlayImpactAudio(
    UWorld* World, const FGameFeedbackSignal& Signal, const FGameFeedbackAudioAction& Action);

bool PlayAttackerConfirmation(
    UWorld* World, const FGameFeedbackSignal& Signal, const FGameFeedbackAudioAction& Action);
```

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `PlayImpactAudio` | `World`、`Signal`、`Action` | `bool` | 已完成同步派发返回 `true`；门禁、DS 或预热中返回 `false` |
| `PlayAttackerConfirmation` | `World`、`Signal`、`Action` | `bool` | 仅本地攻击者播放 2D 确认音；语义同上 |

- **线程**：仅 GameThread；`ResolveVFXSpawnPlan` 为纯函数，可在无头测试中直接调用。
- **生命周期**：软引用未加载时委托 `UGameFeedbackDefinitionProvider::PreloadSoftAsset` 去重预热，本次跳过播放；VFX 生成使用 `ENCPoolMethod::AutoRelease`。
- **失败语义**：Dedicated Server 直接短路返回 `false` / `nullptr`，且不注册任何预热句柄；异步预热不阻塞主线程。

```cpp
const GameFeedback::Execution::FGameFeedbackVFXSpawnPlan Plan =
    GameFeedback::Execution::ResolveVFXSpawnPlan(GetWorld(), Signal, Definition->VFX);

if (Plan.Mode == GameFeedback::Execution::EGameFeedbackVFXSpawnMode::SpawnAtLocation) {
    GameFeedback::Execution::SpawnVFX(GetWorld(), Signal, Definition->VFX);
}
```

```cpp
GameFeedback::Execution::PlayImpactAudio(GetWorld(), Signal, Definition->Audio);
```

## 常见错误与边界

- **私有头文件**：这些类型位于插件 `Private`，其他模块不可包含；跨模块请通过总线与 `UGameFeedbackDefinition` 交互。
- **闪白续期**：同一网格连续命中取各次到期时间的最大值，不会叠加多段闪白。
- **震动分屏**：`FGameFeedbackShakeTracker` 以 `APlayerCameraManager` 为键，只有本地摄像机管理器会产生震动。
- **限流双空实体**：`PassCooldown(nullptr, nullptr, ...)` 返回 `true` 且不写冷却表，不会误伤后续冷却判断。
- **预测窗口**：超过 `0.5s` 或实体失效的预测记录会被清理，网络回执迟到时不再抑制本地表现。
- **执行器返回值**：`false` / `nullptr` 表示本次未同步派发（含预热中），不应在同一帧反复重试。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [网络同步、裁剪与预测去重](../Architecture/NetworkAndCulling.md)
- [快速上手](../Guides/QuickStart.md)
