# GameDebug 插件

**GameDebug** 是面向全项目的模块化游戏调试、GM 作弊指令与诊断工具链插件。它遵循**开发期强劲交互、生产期（Shipping）物理零开销剥离、绝对类型安全**的设计原则。

---

## 一、 核心功能

1. **模块化 GM / 作弊指令中心 (`FGameCheatRegistry`)**：
   - 跨模块注册自定义控制台 GM 指令（如 `GM.AddGold 1000`）。
   - 自动绑定 UE5 原生 `IConsoleManager`（标记 `ECVF_Cheat`）。
   - 内部读写锁保护，支持双引号包裹参数与锁外回调安全派发，防死锁。
   - `Game::Config::bEnableGM` 编译期开关保护，Shipping 构建自动剥离。

2. **现代化屏幕调试打印 (`FGameDebugPrint`)**：
   - 提供 `GAME_DEBUG_PRINT`、`GAME_DEBUG_PRINT_COLOR` 等统一宏。
   - 提供**固定通道防刷屏机制**（`PrintChannel` / `GAME_DEBUG_PRINT_CHANNEL`），同一通道自动计算固定槽位 Key，原地刷新，避免 Tick 高频打印遮挡屏幕。
   - 非 Shipping 编译，Shipping 下宏完全展开为空指令 `((void)0)`，零运行时损耗。

3. **轻量 3D 空间调试绘制 (`FGameDebugDraw`)**：
   - 安全封装 `DrawLine`、`DrawSphere`、`DrawBox`。
   - 内置安全校验（防空 World、防负半径/Extent 导致底层崩溃）。
   - Shipping 构建自动内联剔除。

---

## 二、 目录结构

```
Plugins/GameDebug/
├── Source/
│   └── GameDebug/
│       ├── Public/
│       │   ├── Cheat/
│       │   │   └── FGameCheatRegistry.h
│       │   ├── Print/
│       │   │   └── FGameDebugPrint.h
│       │   ├── Visualizer/
│       │   │   └── FGameDebugDraw.h
│       │   └── GameDebug.h
│       ├── Private/
│       │   ├── Cheat/
│       │   │   └── FGameCheatRegistry.cpp
│       │   ├── Print/
│       │   │   └── FGameDebugPrint.cpp
│       │   ├── Visualizer/
│       │   │   └── FGameDebugDraw.cpp
│       │   ├── Tests/
│       │   │   ├── FGameCheatRegistryTest.cpp
│       │   │   └── FGameDebugPrintTest.cpp
│       │   └── GameDebug.cpp
│       └── GameDebug.Build.cs
├── GameDebug.uplugin
└── README.md
```
