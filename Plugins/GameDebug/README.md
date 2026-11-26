# GameDebug 插件

**GameDebug**（v2.0.0）是面向 Unreal Engine 5（UE 5.1 ~ 5.8）的模块化游戏调试、GM 作弊指令、屏幕诊断与轻量 3D 可视化基础设施插件。它公开仅依赖底层 `GameCore` 与引擎原生模块，私有单向接入全局日志中枢 `GameLogging`（统一分类 `LogGameDebug`），严格遵守单向无环分层矩阵（**零 `Slate` 死依赖、零 `GamePool` 或横向 Level 1 依赖**），并在 Shipping 构建下通过 `GameCore` 编译开关实现物理零开销裁剪。

---

## 一、 核心功能与架构特性

1. **模块化 GM / 作弊指令注册中心 (`FGameCheatRegistry`)**：
   - 跨模块注册自定义控制台 GM 指令（如 `GM.AddGold 1000`），自动绑定引擎 `IConsoleManager`（`ECVF_Cheat`）；
   - **锁序安全与无死锁设计**：严格拒绝空名称或含空白指令名；`ConsoleSyncLock` 串行化控制台注册/注销写路径（零 ABA 竞态），全程在释放 `FRWLock` 后交互 `IConsoleManager` 并按名称 $O(1)$ 注销句柄，彻底根除 AB-BA 锁序倒置，冲突时通过 `LogGameDebug` 输出 `Warning`；
   - **零二次搬移解析 (`ExecuteCheat(FStringView)`)**：先切片提取命令名并经 `FName(View, FNAME_Find)` 零分配短路未命中请求，命中后再单遍解析双引号含空格参数、空双引号 `""` 与 `\"`/`\\` 转义字符并锁外派发；
   - 受 `Game::Config::bEnableGM` 编译期开关控制，Shipping 物理剥离。

2. **域隔离屏幕调试打印 (`FGameDebugPrint`)**：
   - 提供 `Print`、`PrintChannel`、`PrintWarning`、`PrintError`、`GetChannelKey` 及 `do { ... } while (false)` 语句安全宏 `GAME_DEBUG_PRINT*`；
   - **64 位域盐槽位防碰撞**：结合 `GameCore::Game::Hash::Hash64To32` 将低 32 位正交划分（Print 通道恒占 `[0, 0x7FFFFFFF]`，WatchHub 分类恒占 `[0x80000000, 0xFFFFFFFE]`），即便在 `UEngine::ScreenMessages` 的 `int32` 键截断下亦与 `FGameDebugWatchHub` 零碰撞且永不等于 `InvalidKey (MAX_uint64)`；
   - `PrintChannel` 使用栈上 `TStringBuilder<256>` 零临时 `FString` 拼装，自动钳制非法/负驻留时长。

3. **轻量 3D 空间调试图形绘制 (`FGameDebugDraw`)**：
   - 封装 9 大常用 3D 图元：`DrawLine`、`DrawSphere`、`DrawBox`、`DrawCapsule`、`DrawCylinder`、`DrawCone`、`DrawArrow`、`DrawCoordinateSystem`、`DrawString` 及配套 `GAME_DEBUG_DRAW_*` 宏；
   - 全量边界防卫：统一拦截空世界、`NaN`/`Inf` 坐标与尺度、非正半径/半高/张角/字体缩放、非法分段数、零体积盒、起终点重合退化及跨世界/失效 `TestActor`，并对非单位四元数自动安全归一化。

4. **真 $O(1)$ 3D 空间运动轨迹记录器 (`FGameDebugTrailRecorder`)**：
   - 构造期一次分配定长环形缓冲（Shipping 零堆分配），满容量淘汰与过期清理仅推进 `HeadIndex`、零 `memmove`、零稳态堆分配；
   - 支持最小移动距离去重、拦截 `NaN` 坐标与非有限时间戳、对回退时间戳单调非递减钳制，以负数哨兵 `-1.0` 区分平台时钟（完整支持 `t = 0.0` 仿真起始时间）；
   - `DrawTrail` 先行淘汰包括单点在内的全部超期样本并前置校验线宽/时长，外提 `FLinearColor` 转换，按真实剩余生命周期计算时间衰减颜色渐变。

5. **按世界隔离的时间膨胀与单步调试控制器 (`FGameDebugTimeController`)**：
   - 直接操作 `AWorldSettings::SetTimeDilation`，内部以 `FObjectKey` 按 `UWorld` 隔离多 PIE 暂停记忆倍率与步进代次；`StepFrame` 自动补偿 `UWorld::Tick` 中途调用并在世界销毁时即时清理条目；对称提供 `Initialize()` / `Shutdown()`；
   - 明确定义具名契约常量：`MinTimeDilation = 0.001f`、`MaxTimeDilation = 20.0f`、`PausedTimeDilation = 0.0001f`、`PauseDetectionThreshold = 0.0005f`（合法边界 `0.001f` 不被误判为暂停），拦截 `NaN`/`Inf` 与显式非空无效上下文防穿透；
   - 挂载 `GM.TimeScale`、`GM.TimePause`、`GM.TimeResume`、`GM.TimeReset`、`GM.TimeStep` 并严格拒绝尾随杂字符、非正步进帧数或无世界上下文。

6. **高性能运行时调试观察中心 (`FGameDebugWatchHub`)**：
   - 脱离构造期副作用，由模块对称调用 `Initialize()` / `Shutdown()` 挂载与注销 `GM.Watch`；全接口受 `bEnableDebugDraw` 裁剪；
   - `bGlobalEnabled` 采用 `std::atomic<bool>` 消除跨线程数据竞态；关闭分类/全局关闭/清空或项数归零时即时撤销对应屏幕面板，空分类自动回收；`Watch` 返回 `bool`；
   - `TickDisplay` 采用 `TSharedPtr<const FValueGetter>` + 栈内联快照 + `TStringBuilder<512>`，彻底消除每帧 `TFunction` 闭包深拷贝与临时 `FString` 分配，屏幕驻留时长自适应 `FMath::Max(0.1f, DeltaTime * 2.0f)` 防止低帧闪烁。

7. **RAII 作用域耗时诊断秒表 (`FScopedDebugStopwatch`)**：
   - 内部复用 `GameCore::FScopedCpuTimer`（`Cycles64` 硬件时钟）并由 `GAME_ENABLE_CPU_PROFILER` 编译开关精准门控（Shipping 下裁剪为 `sizeof == 1` 空类、零成员零时钟开销）；
   - 离开作用域耗时达到或超出阈值时通过 `LogGameDebug` 输出 `Warning` 并同步在屏幕通道高亮（$\ge 2\times$ 阈值为红色，普通超阈值为橙色）；宏采用 `__COUNTER__` 杜绝同行命名冲突。

8. **蓝图函数库全景支持 (`UGameDebugBlueprintLibrary`)**：
   - 向蓝图统一暴露打印、9 大 3D 图元绘制与 7 项时间控制节点；默认颜色（`DrawDebugCylinder=Orange`、`DrawDebugArrow=Purple`）与 C++ 100% 对齐，并通过内部 `GameDebugWorldResolver` 解除绘制对时间控制器的耦合。

---

## 二、 目录结构

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

---

## 三、 性能基准

实测环境：Apple M5（macOS `27.2.1` arm64，UE `5.8` Development 构建，Apple Clang `21.1.6`，2 轮预热 + 7 轮采样中位数，基线提交 `f29588e`，复现命令 `python3 .scripts/run_automation_tests.py --case Game.Debug`）：

| 基准项 | 中位数耗时 | 均值耗时 | 离散度 (CV%) | 吞吐量 |
|---|---:|---:|---:|---:|
| `FGameDebugDraw` 空世界快速拦截 | `0.94 ns/op` | `0.94 ns/op` | `1.00%` | `1.07 Gops/s` |
| `FGameDebugPrint` 64 位域盐槽位 Key 雪崩散列 | `1.51 ns/op` | `1.51 ns/op` | `1.04%` | `660.62 Mops/s` |
| `FGameDebugTrailRecorder` $O(1)$ 环形写入 (`Cap=64`) | `3.72 ns/op` | `3.76 ns/op` | `3.04%` | `268.94 Mops/s` |
| `FGameDebugTrailRecorder` $O(1)$ 环形写入 (`Cap=256`) | `3.66 ns/op` | `3.69 ns/op` | `1.22%` | `273.34 Mops/s` |
| `FScopedDebugStopwatch` 未超时快路径 (`Cycles64`) | `21.55 ns/op` | `21.63 ns/op` | `0.94%` | `46.41 Mops/s` |
| `FGameCheatRegistry` 未命中零分配短路 | `39.19 ns/op` | `39.21 ns/op` | `0.99%` | `25.52 Mops/s` |
| `FGameCheatRegistry` 命中分词 + 锁外派发 | `121.33 ns/op` | `120.72 ns/op` | `1.30%` | `8.24 Mops/s` |
| `FGameDebugTrailRecorder` 64 点时间渐变绘制 (`DrawTrail`) | `1,123.75 ns/op` | `1,135.92 ns/op` | `3.56%` | `889.88 Kops/s` |
| `FGameDebugWatchHub` 5 组 50 项快照采样 + 格式化 | `6,068.16 ns/op` | `6,072.00 ns/op` | `1.35%` | `164.80 Kops/s` |

---

## 四、 自动化测试与延伸文档

```bash
python3 .scripts/run_automation_tests.py --case Game.Debug
```

- [架构概述与核心设计](Docs/Architecture/Overview.md)
- [公共 API 契约参考手册](Docs/API/GameDebugReference.md)
- [快速上手与最佳实践指南](Docs/Guides/QuickStart.md)
