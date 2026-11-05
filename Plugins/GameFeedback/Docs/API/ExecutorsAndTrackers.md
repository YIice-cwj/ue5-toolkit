# GameFeedback 表现执行器与生命周期追踪器 API 参考

本文档详细介绍 `GameFeedback` 内部负责具体表现逻辑的无状态执行器（Executors）与状态追踪器（Trackers）。

---

## 1. 受击闪白追踪器 (`FGameFeedbackFlashTracker`)

`FGameFeedbackFlashTracker`（私有头文件：`System/GameFeedbackFlashTracker.h`）专管网格体受击 Overlay 材质的生命周期。

### 1.1 核心机制
- **绝对到期时间续期 (`ExpirationRealTime`)**：同一网格体在短时间内连续多次受击时，绝对到期时间更新为当前各次到期时间的最大值，避免后一次较短时长的打击提前还原前一次更长时长的闪白；
- **提前触发重新 Arm**：若定时器较早到期但当前真实时间尚未到达 `ExpirationRealTime`，定时器自动按剩余时间重新挂载（Re-arm），杜绝过早恢复原材质；
- **原始材质强保活 (`TStrongObjectPtr`)**：将网格体原始材质存入强引用智能指针，即便业务层临时清理外部引用触发 GC，原始材质也不会被销毁；
- **三种还原通路**：
  - 自然到期（定时器回调）；
  - `ClearActor`（受击者死亡、换阶段时即时复原）；
  - `Shutdown`（世界切换或组件注销时全量复原）。

---

## 2. 镜头震动追踪器 (`FGameFeedbackShakeTracker`)

`FGameFeedbackShakeTracker`（私有头文件：`System/GameFeedbackShakeTracker.h`）管理本地摄像机的震动实例。

### 2.1 核心机制
- **分屏隔离管理**：以 `TWeakObjectPtr<APlayerCameraManager>` 为键建立字典，本地多玩家分屏时互不干扰；
- **同帧最大振幅择优**：同一帧内多枚子弹或多段爆炸同时命中时，若新震动幅度小于等于本帧已播放的 `MaxScale`，直接跳过；若新振幅更大，则先停止旧震动并启动更强的新震动，避免多重震屏剧烈叠加造成晕眩；
- **局部冲击朝向适配**：当开启 `bOrientShakeToImpactDirection` 时，通过 `CameraRotation.UnrotateVector(Signal.ImpactDirection)` 将世界冲击方向转为摄像机局部坐标系，并以 `ECameraShakePlaySpace::UserDefined` 播放，精准反映受击方向。

---

## 3. 顿帧限流器 (`FGameFeedbackHitStopLimiter`)

`FGameFeedbackHitStopLimiter`（私有头文件：`System/GameFeedbackHitStopLimiter.h`）是一个**无任何 World 依赖的纯逻辑限流器**。

### 3.1 核心机制
- **攻击者/目标滑动窗口冷却 (`PassCooldown`)**：
  - 维护 `(Attacker, Target) -> LastTriggerRealTime` 映射；
  - 冷却时间由 `Action.CooldownInterval` 决定，冷却期内的后续受击直接返回 `false`；
  - 攻击者为空（如毒圈、跌落等环境伤害）同样受独立冷却保护；
- **累积预算控制 (`AcquireDuration`)**：
  - 维护每个 Actor 的 `CumulativeDuration`；
  - 超过 `Action.ResetWindow`（如 0.5s）无受击，累积预算自动重置清零；
  - 申请时长受 `Action.MaxCumulativeDuration` 封顶，预算耗尽返回 0.0f；
- **弱指针失效清理 (`Prune`)**：
  - 定期检查键的 `IsStale()`，安全剔除已被 GC 的 Actor 条目，防止内存泄漏。

---

## 4. 预测指纹历史 (`FGameFeedbackPredictionHistory`)

`FGameFeedbackPredictionHistory`（私有头文件：`System/GameFeedbackPredictionHistory.h`）是一个**纯逻辑去重容器**。

### 4.1 核心机制
- **三要素精确匹配**：`PredictionEventId`（非零）、`Attacker` 弱指针、`Target` 弱指针必须同时相等；
- **0.5s 滑动窗口**：超出 0.5s 的陈旧记录在匹配与入队时被 `Prune` 自动淘汰；
- **命中消费**：通过 `ConsumeMatch` 查找，命中后立即 `RemoveAtSwap` 消费，使后续网络回执不再重复抑制。

---

## 5. 无状态表现执行器 (`GameFeedback::Execution`)

### 5.1 粒子生成器 (`GameFeedbackVFXExecutor.h`)
- **朝向解析 (`ResolveVFXRotation`)**：按 `OrientationMode` 计算世界旋转；若网络回执缺失 `TraceDirection` 或处于范围伤害时，自动优雅降级为冲击法线（`ImpactNormal`），杜绝粒子朝向锁死在世界原点；
- **挂接与对象池**：支持挂接在命中网格体特定骨骼（`bAttachToHitComponent`），使用 `ENCPoolMethod::AutoRelease` 进行 Niagara 对象池自动回收；
- **异步预热**：若 `NiagaraSystem` 尚未加载完成，发起异步加载并跳过本次播放，主线程绝不卡顿。

### 5.2 音效执行器 (`GameFeedbackAudioExecutor.h`)
- **空间打击音 (`PlayImpactAudio`)**：在命中点或目标位置生成，支持衰减与并发限制；
- **攻击者确认音 (`PlayAttackerConfirmation`)**：仅向攻击者本地播放 2D 击中音；
- **异步预热**：软引用音效未加载时在后台异步预热，不阻塞帧率。
