# GameDebug

> **一句话**：GM 指令、域隔离屏幕诊断、3D 绘制、O(1) 轨迹、时间控制、观察中心与作用域秒表的一体化调试设施。
> **版本** v2.0.0 · **引擎** UE 5.1~5.8 · **层级** Level 1 · **直接依赖** GameCore（公开）· GameLogging（私有）

## 30 秒速览

- **解决什么问题**：把散落的 `PrintString`、`DrawDebug*`、控制台 GM 命令与临时耗时计时，收敛成可注册、可开关、可编译期裁剪的统一调试层。
- **核心能力**
  - `FGameCheatRegistry`：模块化 GM / 控制台指令注册与执行。
  - `FGameDebugPrint` + `FGameDebugWatchHub`：域盐隔离的屏幕打印与实时变量观察。
  - `FGameDebugDraw` + `FGameDebugTrailRecorder`：常用 3D 图元与 O(1) 环形轨迹。
  - `FGameDebugTimeController` + `FScopedDebugStopwatch`：按世界时间膨胀 / 单步与 RAII 耗时告警。
  - `UGameDebugBlueprintLibrary`：上述能力的蓝图入口。
- **什么时候用**：开发期 GM 调试、战斗与网络问题定位、运行时数值观察、临时性能热点计时。
- **什么时候不用**：需要落盘、分级或远程上报的正式日志用 `GameLogging`；长期性能剖析用 Unreal Insights 或 `stat`；面向玩家的提示不要走调试打印通道。

## 快速开始

```csharp
PrivateDependencyModuleNames.AddRange(new string[] {
    "GameDebug",
});
```

```cpp
#include "Cheat/FGameCheatRegistry.h"
#include "Print/FGameDebugPrint.h"

FGameCheatRegistry::Get().RegisterCheat(
    FName(TEXT("GM.GiveItem")),
    TEXT("发放测试道具。用法: GM.GiveItem \"Item Name\" <Count>"),
    [](const TArray<FString>& Args) {
        // 参数个数与合法性由业务自行校验
    });

GAME_DEBUG_PRINT_CHANNEL(FName(TEXT("Player")), "Taken Damage: %.1f", 12.5f);
```

观察变量使用 `GAME_DEBUG_WATCH("Player", "Speed", Getter)` 注册，`GAME_DEBUG_UNWATCH` 对称注销；同名通道与观察分组使用正交域盐键，屏幕上互不覆盖。

## 核心能力

### GM / 作弊指令中心

- `FGameCheatRegistry::RegisterCheat` 自动绑定引擎 `IConsoleManager`（`ECVF_Cheat`），并提供 `UnregisterCheat`、`ExecuteCheat(FStringView)`、`HasCheat`、`ClearAll`。
- 拒绝空名称或含空白字符的指令名；`ConsoleSyncLock` 串行化控制台注册 / 注销写路径，`FRWLock` 保护注册表，与控制台交互在锁外执行。
- `ExecuteCheat` 先以 `FName(View, FNAME_Find)` 零分配短路未命中，命中后单遍解析双引号含空格参数、空引号 `""` 与 `\"` / `\\` 转义。
- 受 `Game::Config::bEnableGM` 编译期开关约束，关闭时各入口以 `if constexpr` 短路为无操作。

### 域隔离屏幕打印

- 提供 `Print`、`PrintChannel`、`PrintWarning`、`PrintError`、`GetChannelKey` 与 `do { ... } while (false)` 语句安全宏 `GAME_DEBUG_PRINT*`。
- 64 位域盐槽位：Print 通道占 `[0, 0x7FFFFFFF]`，WatchHub 分类占 `[0x80000000, 0xFFFFFFFE]`，在 `UEngine::ScreenMessages` 的 `int32` 键截断下仍零碰撞，且永不等于 `InvalidKey`。
- `PrintChannel` 使用栈上 `TStringBuilder<256>` 拼装，自动钳制非法或负驻留时长。

### 3D 绘制与轨迹

- `FGameDebugDraw` 封装 `DrawLine`、`DrawSphere`、`DrawBox`、`DrawCapsule`、`DrawCylinder`、`DrawCone`、`DrawArrow`、`DrawCoordinateSystem`、`DrawString` 及 `GAME_DEBUG_DRAW_*` 宏。
- 绘制入口统一拦截空世界、`NaN` / `Inf` 坐标与尺度、非正半径 / 半高 / 张角 / 字体缩放、非法分段数、零体积盒、起终点重合，并对非单位四元数安全归一化。
- `FGameDebugTrailRecorder` 构造期一次分配定长环形缓冲，满容量淘汰与过期清理只推进 `HeadIndex`，稳态零 `memmove`、零堆分配。
- 轨迹支持最小移动距离去重、非有限时间戳拦截、回退时间戳单调非递减钳制；`DrawTrail` 先淘汰超期样本，再按剩余生命周期计算颜色渐变。

### 按世界时间控制

- `FGameDebugTimeController` 直接操作 `AWorldSettings::SetTimeDilation`，以 `FObjectKey` 按 `UWorld` 隔离多 PIE 的暂停记忆倍率与步进代次。
- 具名常量：`MinTimeDilation = 0.001f`、`MaxTimeDilation = 20.0f`、`PausedTimeDilation = 0.0001f`、`PauseDetectionThreshold = 0.0005f`。
- 提供 `SetTimeDilation`、`Pause`、`Resume`、`IsPaused`、`ResetTime`、`StepFrame`，并挂载 `GM.TimeScale`、`GM.TimePause`、`GM.TimeResume`、`GM.TimeReset`、`GM.TimeStep`。
- `StepFrame` 自动补偿 `UWorld::Tick` 中途调用，并在世界销毁时清理对应状态。

### 运行时观察与耗时诊断

- `FGameDebugWatchHub` 由模块对称调用 `Initialize()` / `Shutdown()` 挂载与注销 `GM.Watch`；`bGlobalEnabled` 使用 `std::atomic<bool>`。
- 关闭分类、全局关闭、清空或项数归零时即时撤销对应屏幕面板，空分类自动回收；`Watch` 返回 `bool`。
- `TickDisplay` 使用 `TSharedPtr<const FValueGetter>` + 栈内联快照 + `TStringBuilder<512>`，避免每帧闭包深拷贝与临时 `FString`；驻留时长自适应 `FMath::Max(0.1f, DeltaTime * 2.0f)`。
- `FScopedDebugStopwatch` 复用 `FScopedCpuTimer`（`Cycles64`），由 `GAME_ENABLE_CPU_PROFILER` 门控；超阈值时经 `LogGameDebug` 告警并在屏幕通道高亮（`>= 2x` 阈值为红，普通超阈为橙）。

### 蓝图函数库

- `UGameDebugBlueprintLibrary` 向蓝图暴露打印、3D 图元绘制与时间控制节点，默认颜色与 C++ 侧对齐（如 `DrawDebugCylinder` 为 Orange、`DrawDebugArrow` 为 Purple）。
- 内部通过 `GameDebugWorldResolver` 解析世界上下文，解除绘制对时间控制器的耦合。

## 架构与依赖

| 依赖 | 可见性 | 用途 |
| :--- | :---: | :--- |
| `GameCore` | Public | 编译开关 `Game::Config`、`FScopedCpuTimer`、哈希 |
| `GameLogging` | Private | `LogGameDebug` 分类日志 |
| `Core` / `CoreUObject` / `Engine` | Public | `IConsoleManager`、`ScreenMessages`、`AWorldSettings`、蓝图库 |

| 关键类型 | 角色 |
| :--- | :--- |
| `FGameCheatRegistry` | GM / 控制台指令注册与解析 |
| `FGameDebugPrint` / `FGameDebugWatchHub` | 屏幕打印 / 实时变量观察 |
| `FGameDebugDraw` / `FGameDebugTrailRecorder` | 3D 图元 / 环形轨迹 |
| `FGameDebugTimeController` | 按世界时间膨胀与单步 |
| `FScopedDebugStopwatch` | RAII 作用域耗时告警 |
| `UGameDebugBlueprintLibrary` | 蓝图入口 |

<details>
<summary><b>展开目录结构</b></summary>

```text
Plugins/GameDebug/
├── Docs/
│   ├── Architecture/Overview.md                # 模块分层、域盐键隔离、锁序与环形缓冲设计
│   ├── API/GameDebugReference.md               # 全量 8 大组件公共 API、常量与宏契约手册
│   └── Guides/QuickStart.md                    # C++ / 蓝图 / GM 指令实战接入指南
├── Source/
│   └── GameDebug/
│       ├── Public/
│       │   ├── Cheat/FGameCheatRegistry.h
│       │   ├── Library/UGameDebugBlueprintLibrary.h
│       │   ├── Print/FGameDebugPrint.h
│       │   ├── Profiling/FScopedDebugStopwatch.h
│       │   ├── Time/FGameDebugTimeController.h
│       │   ├── Visualizer/
│       │   │   ├── FGameDebugDraw.h
│       │   │   └── FGameDebugTrailRecorder.h
│       │   ├── Watcher/FGameDebugWatchHub.h
│       │   └── GameDebug.h                     # 模块入口与全量公共聚合门面头
│       ├── Private/
│       │   ├── Core/
│       │   │   ├── GameDebugScreenKeys.h       # 64 位域盐正交屏幕槽位键
│       │   │   ├── GameDebugWorldResolver.h    # 防穿透世界上下文解析器
│       │   │   └── GameDebugWorldResolver.cpp
│       │   ├── Cheat/FGameCheatRegistry.cpp
│       │   ├── Library/UGameDebugBlueprintLibrary.cpp
│       │   ├── Print/FGameDebugPrint.cpp
│       │   ├── Profiling/FScopedDebugStopwatch.cpp
│       │   ├── Time/FGameDebugTimeController.cpp
│       │   ├── Visualizer/
│       │   │   ├── FGameDebugDraw.cpp
│       │   │   └── FGameDebugTrailRecorder.cpp
│       │   ├── Watcher/FGameDebugWatchHub.cpp
│       │   ├── Tests/                          # 16 项自动化真测试与 9 项微基准回归套件
│       │   │   ├── GameDebugTestUtils.h
│       │   │   ├── FGameCheatRegistryTest.cpp
│       │   │   ├── FGameDebugPrintTest.cpp
│       │   │   ├── FGameDebugDrawTest.cpp
│       │   │   ├── FGameDebugTrailRecorderTest.cpp
│       │   │   ├── FGameDebugTimeControllerTest.cpp
│       │   │   ├── FGameDebugWatchHubTest.cpp
│       │   │   ├── FScopedDebugStopwatchTest.cpp
│       │   │   ├── UGameDebugBlueprintLibraryTest.cpp
│       │   │   └── GameDebugPerformanceTest.cpp
│       │   └── GameDebug.cpp                   # 注册 LogGameDebug 与对称模块生命周期
│       └── GameDebug.Build.cs
├── GameDebug.uplugin
└── README.md
```

</details>

## 性能与边界

<details>
<summary>展开性能基准（历史 Development 构建参考值）</summary>

实测环境：Apple M5（macOS `27.2.1` arm64，UE `5.8` Development 构建，Apple Clang `21.1.6`，2 轮预热 + 7 轮采样中位数，基线提交 `f29588e`，复现命令 `python3 .scripts/run_automation_tests.py --case Game.Debug`）：

| 基准项 | 中位数耗时 | 均值耗时 | 离散度 (CV%) | 吞吐量 |
|---|---:|---:|---:|---:|
| `FGameDebugDraw` 空世界快速拦截 | `0.94 ns/op` | `0.94 ns/op` | `1.00%` | `1.07 Gops/s` |
| `FGameDebugPrint` 64 位域盐槽位 Key 雪崩散列 | `1.51 ns/op` | `1.51 ns/op` | `1.04%` | `660.62 Mops/s` |
| `FGameDebugTrailRecorder` O(1) 环形写入 (`Cap=64`) | `3.72 ns/op` | `3.76 ns/op` | `3.04%` | `268.94 Mops/s` |
| `FGameDebugTrailRecorder` O(1) 环形写入 (`Cap=256`) | `3.66 ns/op` | `3.69 ns/op` | `1.22%` | `273.34 Mops/s` |
| `FScopedDebugStopwatch` 未超时快路径 (`Cycles64`) | `21.55 ns/op` | `21.63 ns/op` | `0.94%` | `46.41 Mops/s` |
| `FGameCheatRegistry` 未命中零分配短路 | `39.19 ns/op` | `39.21 ns/op` | `0.99%` | `25.52 Mops/s` |
| `FGameCheatRegistry` 命中分词 + 锁外派发 | `121.33 ns/op` | `120.72 ns/op` | `1.30%` | `8.24 Mops/s` |
| `FGameDebugTrailRecorder` 64 点时间渐变绘制 (`DrawTrail`) | `1,123.75 ns/op` | `1,135.92 ns/op` | `3.56%` | `889.88 Kops/s` |
| `FGameDebugWatchHub` 5 组 50 项快照采样 + 格式化 | `6,068.16 ns/op` | `6,072.00 ns/op` | `1.35%` | `164.80 Kops/s` |

</details>

- **Shipping 裁剪**：`GAME_DEBUG_PRINT*` / `GAME_DEBUG_WATCH` / `GAME_DEBUG_DRAW_*` 在 `GAME_ENABLE_DEBUG_DRAW == 0` 时展开为 `((void)0)`，实参不求值；`FScopedDebugStopwatch` 在 `GAME_ENABLE_CPU_PROFILER == 0` 时成员与时钟全部移除。
- **线程**：屏幕打印、3D 绘制、时间控制与观察面板刷新依赖引擎 GameThread 侧 API；`FGameDebugWatchHub` 的全局开关为原子变量，注册表由读写锁保护。
- **生命周期**：`FGameDebugWatchHub` 与 `FGameDebugTimeController` 由模块启动 / 卸载对称调用 `Initialize()` / `Shutdown()`；`FGameCheatRegistry::ClearAll()` 用于模块卸载或测试清理。
- **缓冲边界**：`FGameDebugTrailRecorder` 的容量在构造期固定（默认 64），生命周期与最小距离下限分别为 `0.1s` 与 `0.0`。
- **时间倍率**：`FGameDebugTimeController` 合法范围为 `[0.001f, 20.0f]`，暂停写入 `0.0001f`，`0.001f` 不会被误判为暂停。
- **性能数据**：均为历史 Development 实测参考值，未纳入 CI 自动复测，不构成 Shipping 性能承诺。

## 文档导航

| 文档 | 内容 |
| :--- | :--- |
| [架构概述](Docs/Architecture/Overview.md) | 模块分层、域盐键隔离、锁序与环形缓冲设计 |
| [API 参考](Docs/API/GameDebugReference.md) | 8 大组件公共 API、常量与宏契约 |
| [快速开始](Docs/Guides/QuickStart.md) | C++ / 蓝图 / GM 指令接入指南 |

自动化回归：`python3 .scripts/run_automation_tests.py --case Game.Debug`（16 项功能测试 + 9 项微基准）。
