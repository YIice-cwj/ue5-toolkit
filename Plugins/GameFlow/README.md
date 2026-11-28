# GameFlow

> **一句话**：关卡流转状态机、代际异步就绪屏障、加权进度与遥测，以及 C++20 协程 / 蓝图等待原语。
> **版本** v2.0.0 · **引擎** UE 5.1~5.8 · **层级** Level 2 · **直接依赖** GameCore（公开）· GameLogging（私有）

## 30 秒速览

- **解决什么问题**：把一次 `OpenLevel` 扩展成可观测的加载流程——显示幕布、等待对象池预热 / 资源预载 / 网络握手等异步子系统投票、超时熔断，并让 C++ 协程与蓝图都能等到真正就绪。
- **核心能力**
  - `ULoadingSubsystem`：`Idle` / `LoadingMap` / `WaitingBarriers` 三态关卡流转。
  - `FLoadingBarrierToken`：栈上值语义、零堆分配的 RAII 就绪屏障。
  - O(1) 加权进度、全局 + 单屏障双轨看门狗与耗时遥测。
  - `GameFlow::WaitLoadingComplete`：跨线程取消安全的 C++20 协程等待器。
  - `UAsyncAction_WaitForLoadingComplete`：带 `OnCompleted` / `OnCancelled` 的蓝图节点。
- **什么时候用**：大关卡、Boss 战、副本跳转等需要加载幕布，且地图加载完成后仍需等待异步业务就绪的场景。
- **什么时候不用**：同地图子关卡流式加载与 World Partition 分区由 `ULevelStreaming` / World Partition 负责；只需最简单切图可直接用 `UGameplayStatics::OpenLevel`；网络连接与重连交给 GameNetService。

## 快速开始

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core",
    "CoreUObject",
    "Engine",
    "GameCore",
    "GameFlow",
});
```

```cpp
#include "System/ULoadingSubsystem.h"

void OpenBattleLevel(const UObject* WorldContext) {
    if (ULoadingSubsystem* Loading = ULoadingSubsystem::Get(WorldContext)) {
        Loading->OpenLevelWithLoading(FName(TEXT("/Game/Maps/BattleArena")));
    }
}
```

地图加载完成后仍需保持幕布时，申请栈上屏障令牌：

```cpp
FLoadingBarrierToken Barrier = Loading->AcquireBarrierToken(
    FName(TEXT("ArenaWarmup")),
    2.0f,
    10.0f
);

Barrier.ReportProgress(0.5f, NSLOCTEXT("Loading", "WarmupMeshes", "正在预热战场资源..."));
Barrier.Release();
```

协程侧使用 `co_await GameFlow::WaitLoadingComplete(Host, Token, 15.0f)`，返回 `true` 表示地图与全部屏障已解除。

## 核心能力

### 三态关卡流转

- `ULoadingSubsystem` 继承 `UGameInstanceSubsystem`，用 `ELoadingTransitionPhase`（`Idle` / `LoadingMap` / `WaitingBarriers`）管理跨关卡生命周期。
- 挂钩 `FCoreUObjectDelegates::PreLoadMapWithContext` 与 `PostLoadMapWithWorld`，按 `OwningGameInstance` 隔离多 PIE，并处理 `PostLoadMapWithWorld(nullptr)` 的地图加载失败。
- `Deinitialize()` 区分正常完成与销毁中止：空闲态静默清理，加载期仅触发 `OnLoadingAbortedNative` 并清空全部委托。
- 同时提供蓝图动态委托与原生委托：`OnLoadingStateChanged`、`OnLoadingProgressChanged`、`OnLoadingTelemetryReport`、`OnLoadingAbortedNative`。

### 代际就绪屏障

- 底层使用 `TSlotMap<FActiveBarrierInfo, FLoadingBarrierTag>` 与 64 位强类型句柄 `FLoadingBarrierHandle`（32 位槽位 + 32 位代际），密集存储、O(1) 查找并防 ABA。
- `FLoadingBarrierToken` 为栈上值语义、仅可移动、零堆分配；析构自动释放，`Release()` 幂等，`IsValid()` 为 O(1) `noexcept`。
- 权重下限截断至 `0.1f`，非有限值回落 `1.0f`；单屏障超时 `0` 表示仅受全局看门狗约束，`> 0` 时独立生效。
- 蓝图对应 `AcquireLoadingBarrierBp`、`ReleaseLoadingBarrierBp`、`ReportBarrierProgressBp`，全入口拦截 `NaN` / `Inf`。

### 加权进度、看门狗与遥测

- `FinishedBarriersTotalWeight` 做 O(1) 增量缓存，进度重算只线性扫描当前活跃屏障；非加载态释放屏障不写入遥测数组。
- 全局总超时与单屏障超时由 `CustomTimeoutBarrierCount` 联动定时器启停；即使关闭全局超时，单屏障超时仍独立生效；巡检使用 `TInlineAllocator<8>` 零堆分配。
- 单次流转结束广播 `OnLoadingTelemetryReport` / `OnLoadingTelemetryReportNative`，并缓存到 `GetLastTelemetryReport()`。
- 报告包含 `TotalDurationMs`、`MapLoadDurationMs`、`bTimedOut` 与逐屏障 `FLoadingBarrierTelemetry` 明细。

### C++20 协程等待

- 统一签名：`GameFlow::WaitLoadingComplete(WorldContextObject, CancellationToken = None, TimeoutSeconds = 0.0f)`。
- 非加载态、预取消、无效上下文经内联状态零堆分配同步直通；真正挂起时单次 `MakeShared` 托管等待状态。
- 后台线程取消通过 `FGameThreadTaskRunner::RunOnGameThread` 回投主线程清理并恢复协程；`FGameCoroStateBase::AddCompletionCallback` 覆盖 `Task.Cancel()` 与宿主销毁。
- 正常完成或本就非加载态返回 `true`；预取消、中途取消、超时、上下文无效或子系统销毁中止返回 `false`。

### 蓝图异步等待节点

- `UAsyncAction_WaitForLoadingComplete::WaitForLoadingComplete` 提供 `OnCompleted` 与 `OnCancelled` 双执行引脚，取消、无效上下文与子系统销毁均走 `OnCancelled`。
- `Activate()` 幂等；广播前先解绑委托防重入；`BeginDestroy()` 兜底清理。
- 通过 `Subsystem->GetGameInstance()` 完成 GC 注册保活。

## 架构与依赖

| 依赖 | 可见性 | 用途 |
| :--- | :---: | :--- |
| `GameCore` | Public | `TSlotMap`、`TGameHandle`、`FGameThreadTaskRunner`、协程原语 |
| `GameLogging` | Private | `LogGameFlow` 分类日志 |
| `Core` / `CoreUObject` / `Engine` | Public | `UGameInstanceSubsystem`、地图加载委托、定时器 |

| 关键类型 | 角色 |
| :--- | :--- |
| `ULoadingSubsystem` | 关卡流转与屏障汇聚中枢 |
| `FLoadingBarrierToken` | 栈上 RAII 屏障令牌 |
| `FLoadingBarrierHandle` | 64 位代际句柄 |
| `FLoadingTelemetryReport` | 单次流转遥测报告 |
| `FWaitLoadingCompleteAwaiter` | C++20 协程等待器 |
| `UAsyncAction_WaitForLoadingComplete` | 蓝图异步节点 |

<details>
<summary><b>展开目录结构</b></summary>

```text
Plugins/GameFlow/
├── Docs/
│   ├── Architecture/Overview.md                      # 分层架构、多 PIE 隔离、TSlotMap 与跨线程安全设计
│   ├── API/GameFlowReference.md                      # 全量公共 API、委托、结构体与契约参考手册
│   └── Guides/QuickStart.md                          # C++ / 协程 / 蓝图接入实战指南
├── Source/
│   └── GameFlow/
│       ├── Public/
│       │   ├── Core/GameFlowTypes.h                  # 强类型代际句柄、三态枚举、遥测结构体与多播委托
│       │   ├── System/
│       │   │   ├── FLoadingBarrierToken.h            # 栈上值语义零堆分配 RAII 屏障令牌
│       │   │   └── ULoadingSubsystem.h               # 关卡流转与异步就绪屏障子系统
│       │   ├── Async/FWaitLoadingCompleteAwaiter.h   # C++20 协程等待原语
│       │   ├── Blueprint/AsyncAction_WaitForLoadingComplete.h  # 蓝图异步等待动作节点
│       │   └── GameFlow.h                            # 运行时模块接口
│       ├── Private/
│       │   ├── System/
│       │   │   ├── FLoadingBarrierToken.cpp
│       │   │   └── ULoadingSubsystem.cpp
│       │   ├── Async/FWaitLoadingCompleteAwaiter.cpp
│       │   ├── Blueprint/AsyncAction_WaitForLoadingComplete.cpp
│       │   ├── Tests/                                # 15 项自动化测试与 7 项微基准套件
│       │   │   ├── GameFlowTestUtils.h               # RAII 独立测试世界守卫
│       │   │   ├── ULoadingTestReceiver.h            # 私有隐藏蓝图动态引脚接收器
│       │   │   ├── LoadingSubsystemTest.cpp
│       │   │   ├── LoadingBarrierTokenTest.cpp
│       │   │   ├── LoadingCoroAwaiterTest.cpp
│       │   │   └── GameFlowPerformanceTest.cpp
│       │   └── GameFlow.cpp                          # 注册 LogGameFlow 与模块生命周期日志
│       └── GameFlow.Build.cs
├── GameFlow.uplugin
└── README.md
```

</details>

## 性能与边界

<details>
<summary>展开性能基准（历史 Development 构建参考值）</summary>

实测环境：Apple M5（macOS `27.2.1` arm64，UE `5.8` Development 构建，Clang `21.1.6` / Apple Clang `21.0.0`，2 轮预热 + 7 轮采样中位数，基线提交 `bfcbac4`，复现命令 `python3 .scripts/run_automation_tests.py --case Game.Flow`）：

| 基准项 | 中位数耗时 | 均值耗时 | 离散度 (CV%) | 吞吐量 |
|---|---:|---:|---:|---:|
| `WatchdogSweep`（8 路独立超时屏障无超时热巡检） | `11.09 ns/op` | `11.56 ns/op` | `8.00%` | `90.14 Mops/s` |
| `BarrierBpLifecycle`（蓝图 64 位代际句柄申请+释放） | `16.18 ns/op` | `16.22 ns/op` | `4.69%` | `61.81 Mops/s` |
| `BarrierTokenLifecycle`（C++ 栈上 `TSlotMap` RAII 申请+释放） | `19.77 ns/op` | `20.27 ns/op` | `9.44%` | `50.57 Mops/s` |
| `ProgressRecalculate`（8 活跃 + 32 已完成屏障加权进度重算与广播） | `63.38 ns/op` | `67.80 ns/op` | `11.22%` | `15.78 Mops/s` |
| `CoroPassThrough`（非加载态 C++20 协程零堆分配直通） | `112.47 ns/op` | `112.02 ns/op` | `1.04%` | `8.89 Mops/s` |
| `LoadingDelegateBroadcast`（原生 + 蓝图动态多播双向 4 次派发） | `464.29 ns/op` | `467.40 ns/op` | `3.45%` | `2.15 Mops/s` |
| `CoroSuspendResume`（加载态真实挂起 + 事件唤醒 + 全清理往返） | `1,061.95 ns/op` | `1,136.80 ns/op` | `14.52%` | `0.94 Mops/s` |

</details>

- **线程**：子系统、屏障令牌与加载委托均在 GameThread 收口；后台取消通过 `RunOnGameThread` 回投后统一清理。
- **超时**：全局看门狗默认 `30s`；`SetMaxLoadingTimeoutSeconds(<= 0)` 关闭全局超时，但单屏障 `InTimeoutSeconds > 0` 仍独立熔断。
- **令牌生命周期**：`FLoadingBarrierToken` 仅可移动、不可拷贝；看门狗熔断或外部释放后 `IsValid()` 立即返回 `false`，栈上令牌析构不再重复释放。
- **多 PIE**：流转状态按 `OwningGameInstance` 隔离；无 `GameInstance` 的编辑器预览 / 缩略图世界不参与流转。
- **遥测边界**：只有加载态内完成或超时的屏障会进入遥测明细，非加载态释放的屏障不污染报告。
- **性能数据**：均为历史 Development 实测参考值，未纳入 CI 自动复测，不构成 Shipping 性能承诺。

## 文档导航

| 文档 | 内容 |
| :--- | :--- |
| [架构概述](Docs/Architecture/Overview.md) | 分层架构、多 PIE 隔离、`TSlotMap` 与跨线程安全设计 |
| [API 参考](Docs/API/GameFlowReference.md) | 公共 API、委托、结构体与契约 |
| [快速开始](Docs/Guides/QuickStart.md) | C++ / 协程 / 蓝图接入指南 |

自动化回归：`python3 .scripts/run_automation_tests.py --case Game.Flow`（15 项自动化测试 + 7 项微基准）。
