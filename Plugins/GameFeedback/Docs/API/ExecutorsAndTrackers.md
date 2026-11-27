# GameFeedback 表现执行器与生命周期追踪器 API 参考

本文档详细介绍 `GameFeedback` 内部负责具体表现逻辑的无状态执行器（`Private/Execution/`）与状态追踪器（`Private/Tracker/`）。

---

## 1. 受击闪白追踪器 (`FGameFeedbackFlashTracker`)

`FGameFeedbackFlashTracker`（私有头文件：`Tracker/GameFeedbackFlashTracker.h`）专管网格体受击 Overlay 材质的生命周期。

### 1.1 核心机制
- **绝对到期时间续期 (`ExpirationRealTime`)**：同一网格体在短时间内连续多次受击时，绝对到期时间更新为当前各次到期时间的最大值；
- **弱存活令牌防 UAF 与析构兜底**：定时器回调绑定 `TWeakPtr<bool>` 存活令牌，且 `~FGameFeedbackFlashTracker()` 自动兜底 `Shutdown()`，杜绝异常卸载或 GC 引发的裸指针 UAF；
- **零堆分配收集与定时器回收**：采用 `TArray<UMeshComponent*, TInlineAllocator<4>>` 收集候选网格；`PruneInvalid(World)` 同步取消失效网格对应的定时器句柄；
- **原始材质强保活 (`TStrongObjectPtr`)**：确保还原通路（自然到期、`ClearActor`、`Shutdown`）在 GC 后依然安全可用。

---

## 2. 镜头震动追踪器 (`FGameFeedbackShakeTracker`)

`FGameFeedbackShakeTracker`（私有头文件：`Tracker/GameFeedbackShakeTracker.h`）管理本地摄像机的震动实例。

### 2.1 核心机制
- **分屏隔离管理**：以不可变对象键 `TObjectKey<APlayerCameraManager>` 为键建立字典，本地多玩家分屏互不干扰，失效对象按 `IsValid(ResolveObjectPtr())` 自动清理；
- **同帧最大振幅择优**：同一帧内仅保留最高振幅震动，弱振幅直接抑制；
- **局部冲击朝向适配**：开启 `bOrientShakeToImpactDirection` 时将世界冲击方向转为摄像机局部坐标系播放。

---

## 3. 顿帧限流器 (`FGameFeedbackHitStopLimiter`)

`FGameFeedbackHitStopLimiter`（私有头文件：`Tracker/GameFeedbackHitStopLimiter.h`）是一个**无任何 World 依赖的纯逻辑限流器**。

### 3.1 核心机制
- **攻击者/目标滑动窗口冷却 (`PassCooldown`)**：维护 `(TObjectKey<AActor>, TObjectKey<AActor>) -> LastTriggerRealTime` 映射；双空实体早退不污染冷却表；
- **累积预算与长重置窗口保护 (`AcquireDuration`)**：维护每个 Actor 的 `CumulativeDuration` 与实际 `ResetWindow`，`Prune` 过期寿命取 `FMath::Max(5.0, Track.ResetWindow)` 防止提前清空长重置窗口；
- **节流清理 (`PruneIfNeeded` / `Reset`)**：按 `1.0s` 时间间隔或条目数 ≥ 64 节流清理失效 `TObjectKey`（`!IsValid(ResolveObjectPtr())`）与超期记录，避免每次命中全表扫描。

---

## 4. 预测指纹历史 (`FGameFeedbackPredictionHistory`)

`FGameFeedbackPredictionHistory`（私有头文件：`Tracker/GameFeedbackPredictionHistory.h`）是一个**纯逻辑有界去重容器**。

### 4.1 核心机制
- **栈内联有界缓冲**：底层采用 `TArray<FEntry, TInlineAllocator<16>>`，设置安全容量上限 `MaxEntries = 128`（满时淘汰最旧时间戳条目防无界增长）；
- **三要素精确匹配与无序交换清理**：`PredictionEventId`（非零）、`Attacker` 与 `Target` 必须同时相等；`Prune` 使用 `RemoveAllSwap` 同步淘汰超过 `0.5s` 窗口及 `IsStale()` 失效弱引用记录。

---

## 5. 无状态表现执行器 (`GameFeedback::Execution`)

### 5.1 粒子执行器 (`Execution/GameFeedbackVFXExecutor.h`)
- **纯逻辑决策分离 (`ResolveVFXSpawnPlan`)**：先纯逻辑推导 `Skipped / AsyncPreload / SpawnAtLocation / SpawnAttached` 及完整位姿，支持 `-nullrhi` 无头自动化测试精确验证；
- **朝向解析与归一化反射 (`ResolveVFXRotation`)**：反射法线模式先对 `ImpactNormal` 归一化，缺失轨迹向量时优雅降级为法线朝向；
- **统一去重预热与对象池**：软引用未加载时委托 `UGameFeedbackDefinitionProvider::PreloadSoftAsset` 去重预热（零句柄泄漏）；生成时采用 `ENCPoolMethod::AutoRelease`。

### 5.2 音效执行器 (`Execution/GameFeedbackAudioExecutor.h`)
- **空间打击音 (`PlayImpactAudio`)** 与 **攻击者 2D 确认音 (`PlayAttackerConfirmation`)**：返回 `bool` 表示是否完成同步派发；软引用未加载时委托 `UGameFeedbackDefinitionProvider::PreloadSoftAsset` 去重预热，Dedicated Server 直接短路返回 `false` 且不注册任何预热句柄。
