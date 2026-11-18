# GameDebug 插件

**GameDebug** 是面向全项目的模块化游戏调试、GM 作弊指令与诊断工具链插件。它遵循**开发期强劲交互、生产期（Shipping）物理零开销剥离、绝对类型安全**的设计原则。

---

## 一、 核心功能

1. **模块化 GM / 作弊指令中心 (`FGameCheatRegistry`)**：
   - 跨模块注册自定义控制台 GM 指令（如 `GM.AddGold 1000`）；
   - 自动绑定 UE5 原生 `IConsoleManager`（标记 `ECVF_Cheat`）；
   - 内部读写锁保护，支持双引号包裹含空格参数及 `\"`、`\\` 转义字符，锁外安全回调派发，彻底防死锁；
   - 统一由 `Game::Config::bEnableGM` 编译期开关控制，Shipping 构建物理剔除。

2. **现代化屏幕调试打印 (`FGameDebugPrint`)**：
   - 提供 `GAME_DEBUG_PRINT`、`GAME_DEBUG_PRINT_COLOR`、`GAME_DEBUG_PRINT_WARN`、`GAME_DEBUG_PRINT_ERROR` 统一宏；
   - 提供**固定通道防刷屏机制**（`PrintChannel` / `GAME_DEBUG_PRINT_CHANNEL`），同一通道自动计算固定槽位 Key，原地刷新，避免高频打印遮挡屏幕；
   - 宏体系由 `GAME_ENABLE_DEBUG_DRAW` 控制，Shipping 构建下宏完全展开为空指令 `((void)0)`，零运行时损耗。

3. **轻量 3D 空间调试图形绘制 (`FGameDebugDraw`)**：
   - 封装 9 大常用 3D 几何图元：`DrawLine`、`DrawSphere`、`DrawBox`、`DrawCapsule`、`DrawCylinder`、`DrawCone`、`DrawArrow`、`DrawCoordinateSystem`、`DrawString`；
   - 内置安全校验（防空 World、防负半径/半高/Extent/长度、防零朝向矢量崩溃）；
   - 提供 `GAME_DEBUG_DRAW_*` 完整宏封装体系，Shipping 构建物理剔除全部调用与实参计算。

4. **现代化通用运行时调试观察中心 (`FGameDebugWatchHub`)**：
   - 支持跨模块以非侵入式方式按分类注册任意变量与状态采样闭包（`GAME_DEBUG_WATCH` / `GAME_DEBUG_UNWATCH`）；
   - 提供 `GetWatchItemCount` 接口精确掌握各分类活跃项容量；
   - 自动挂载控制台指令 `GM.Watch list | all <on/off> | <Category> <on/off>`，按需启闭显示；
   - 锁外安全快照派发，避免采样闭包重入死锁。

5. **时钟膨胀、暂停记忆与逐帧单步调试控制器 (`FGameDebugTimeController`)**：
   - 提供全局世界时钟平滑缩放、暂停（自动记忆暂停前倍率）、恢复（优先恢复暂停前倍率）、判定暂停（`IsPaused`）、强制重置为 1.0x（`ResetTime`）以及单步逐帧推进（`StepFrame`）；
   - 自动挂载 GM 指令：`GM.TimeScale <倍率>`（如 0.1 慢动作）、`GM.TimePause`（暂停冻结）、`GM.TimeResume`（恢复记忆速度）、`GM.TimeReset`（重置为 1.0x）、`GM.TimeStep [帧数]`（单步步进）。

6. **3D 空间运动轨迹记录器 (`FGameDebugTrailRecorder`)**：
   - 专为弹道投掷物、突进位移技能、网络预测回滚与怪物寻路设计的运动轨迹记录器；
   - 支持最小移动距离去重、固定容量环形淘汰、生命周期自动过期与时间衰减颜色渐变平滑连线（支持指定单帧或持续 `Duration` 秒数）。

7. **RAII 作用域耗时诊断秒表 (`FScopedDebugStopwatch`)**：
   - 栈上 RAII 耗时秒表，离开作用域时若超出毫秒阈值，自动向指定通道与控制台派发高亮报警；
   - 提供 `GAME_DEBUG_SCOPED_TIMER` 与 `GAME_DEBUG_SCOPED_TIMER_CHANNEL` 零开销宏。

8. **蓝图函数库全景支持 (`UGameDebugBlueprintLibrary`)**：
   - 向蓝图、策划与 TA 完整开放通道打印、9 大 3D 图元绘制与时钟控制器全部能力（`SetGlobalTimeDilation`、`GetGlobalTimeDilation`、`PauseGameTime`、`ResumeGameTime`、`IsGamePaused`、`ResetGameTime`、`StepGameFrames`）；
   - 内部统一受 `Game::Config::bEnableDebugDraw` 控制，Shipping 下编译器自动消除无谓调用。

---

## 二、 目录结构

```
Plugins/GameDebug/
├── Source/
│   └── GameDebug/
│       ├── Public/
│       │   ├── Cheat/
│       │   │   └── FGameCheatRegistry.h
│       │   ├── Library/
│       │   │   └── UGameDebugBlueprintLibrary.h
│       │   ├── Print/
│       │   │   └── FGameDebugPrint.h
│       │   ├── Profiling/
│       │   │   └── FScopedDebugStopwatch.h
│       │   ├── Time/
│       │   │   └── FGameDebugTimeController.h
│       │   ├── Visualizer/
│       │   │   ├── FGameDebugDraw.h
│       │   │   └── FGameDebugTrailRecorder.h
│       │   ├── Watcher/
│       │   │   └── FGameDebugWatchHub.h
│       │   └── GameDebug.h
│       ├── Private/
│       │   ├── Cheat/
│       │   │   └── FGameCheatRegistry.cpp
│       │   ├── Library/
│       │   │   └── UGameDebugBlueprintLibrary.cpp
│       │   ├── Print/
│       │   │   └── FGameDebugPrint.cpp
│       │   ├── Profiling/
│       │   │   └── FScopedDebugStopwatch.cpp
│       │   ├── Time/
│       │   │   └── FGameDebugTimeController.cpp
│       │   ├── Visualizer/
│       │   │   ├── FGameDebugDraw.cpp
│       │   │   └── FGameDebugTrailRecorder.cpp
│       │   ├── Watcher/
│       │   │   └── FGameDebugWatchHub.cpp
│       │   ├── Tests/
│       │   │   ├── FGameCheatRegistryTest.cpp
│       │   │   ├── FGameDebugPrintTest.cpp
│       │   │   ├── FGameDebugDrawTest.cpp
│       │   │   ├── FGameDebugWatchHubTest.cpp
│       │   │   ├── FGameDebugLibraryAndTimeTest.cpp
│       │   │   └── GameDebugPerformanceTest.cpp
│       │   └── GameDebug.cpp
│       └── GameDebug.Build.cs
├── GameDebug.uplugin
└── README.md
```
