# GameFlow 插件

**GameFlow**（v2.0.0）是面向 Unreal Engine 5（UE 5.1 ~ 5.8）的轻量级关卡流转状态机、代际异步就绪屏障、蓝图异步等待节点与跨线程安全 C++20 协程等待原语插件。它归属 **Level 2 领域调度层**，公开仅依赖 `GameCore` 与引擎基础模块，私有单向接入全局日志中枢 `GameLogging`（统一日志分类 `LogGameFlow`），零 `GamePool` / `UMG` / `Slate` 侵入依赖。

---

## 一、 核心功能与架构特性

1. **三态关卡流转中枢 (`ULoadingSubsystem`)**：
   - 继承自 `UGameInstanceSubsystem`，通过显式三态枚举 `ELoadingTransitionPhase`（`Idle` / `LoadingMap` / `WaitingBarriers`）管理跨关卡加载生命周期；
   - 挂钩 `FCoreUObjectDelegates::PreLoadMapWithContext` 与 `PostLoadMapWithWorld`，严格按 `OwningGameInstance` 隔离多 PIE 实例（Listen Server + Client 零串扰）与无 `GameInstance` 的编辑器预览/缩略图世界，并兜底处理 `PostLoadMapWithWorld(nullptr)` 地图加载失败场景；
   - `Deinitialize()` 严格区分正常完成与销毁中止：空闲态静默清理零广播，加载期仅触发 `OnLoadingAbortedNative` 并清空全部动态与原生委托。

2. **零堆分配代际就绪屏障 (`FLoadingBarrierToken` + `GameCore::TSlotMap`)**：
   - 底层采用 `GameCore::TSlotMap<FActiveBarrierInfo, FLoadingBarrierTag>` 与 64 位强类型代际句柄 `FLoadingBarrierHandle`：连续密集数组存储、$O(1)$ 无哈希查找与 32 位代际防 ABA 碰撞；
   - C++ 统一使用栈上值语义仅可移动的 `FLoadingBarrierToken`（零堆分配），`IsValid()` $O(1)$ `noexcept` 直查子系统槽位表，看门狗熔断后立即返回 `false`；
   - 蓝图配套提供 `AcquireLoadingBarrierBp` / `ReleaseLoadingBarrierBp` / `ReportBarrierProgressBp`，全入口拦截 `NaN` / `Inf` 非有限浮点输入。

3. **O(1) 加权平滑进度、双轨看门狗与完整遥测 (`FLoadingTelemetryReport`)**：
   - 维护 `FinishedBarriersTotalWeight` $O(1)$ 增量权重缓存，进度重算仅线性扫描当前活跃屏障；非加载态释放屏障不写入遥测数组，零内存泄漏；
   - 全局总超时与单屏障独立超时由 `CustomTimeoutBarrierCount` $O(1)$ 自动联动定时器启停（即使关闭全局超时，单屏障超时依然独立生效），看门狗巡检使用栈内联缓冲 `TInlineAllocator<8>` 零堆分配；
   - 单次流转结束同步广播动态委托 `OnLoadingTelemetryReport` 与原生委托 `OnLoadingTelemetryReportNative`，并缓存至 `GetLastTelemetryReport()`，完整包含 `TotalDurationMs`、`MapLoadDurationMs`、`bTimedOut` 与逐屏障明细。

4. **零直通分配、跨线程安全的 C++20 协程等待器 (`GameFlow::WaitLoadingComplete`)**：
   - 统一单一规范签名：`WaitLoadingComplete(WorldContextObject, CancellationToken = None, TimeoutSeconds = 0.0f)`；
   - 非加载态 / 预取消 / 无效上下文经内联状态零堆分配同步直通；挂起期单次 `MakeShared` 托管全部句柄；
   - 后台工作线程触发 `FCancellationToken::Cancel()` 自动经 `GameCore::FGameThreadTaskRunner::RunOnGameThread` 回投主线程清理与恢复协程；挂钩 `FGameCoroStateBase::AddCompletionCallback` 确保 `Task.Cancel()` 与宿主销毁时零委托/零定时器泄漏。

5. **重入安全蓝图异步等待节点 (`UAsyncAction_WaitForLoadingComplete`)**：
   - 提供 `OnCompleted` 与 `OnCancelled` 双执行引脚（无效上下文、主动 `Cancel()` 或子系统销毁中止均走 `OnCancelled` 收口，绝不误报完成）；
   - `Activate()` 幂等、广播前先行解绑防重入、`BeginDestroy()` 兜底清理，直连 `Subsystem->GetGameInstance()` 完成 GC 注册保活。

---

## 二、 目录结构

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

---

## 三、 性能基准

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

---

## 四、 自动化测试与延伸文档

```bash
python3 .scripts/run_automation_tests.py --case Game.Flow
```

- [架构概述与核心设计](Docs/Architecture/Overview.md)
- [公共 API 契约参考手册](Docs/API/GameFlowReference.md)
- [快速上手指南](Docs/Guides/QuickStart.md)
