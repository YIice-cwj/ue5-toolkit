# GameDebug 公共 API 契约参考手册

统一聚合门面头：`#include "GameDebug.h"`（亦可按需包含单组件头文件）。

---

## 1. `FGameCheatRegistry` (`Cheat/FGameCheatRegistry.h`)

编译期开关：`Game::Config::bEnableGM`（Shipping 物理剥离）。

| 接口 | 签名 | 契约说明 |
|---|---|---|
| 单例访问 | `static FGameCheatRegistry& Get()` | 获取全局 GM 注册表引用 |
| 注册指令 | `bool RegisterCheat(FName CommandName, const FString& HelpText, FCheatHandler Handler)` | 拒绝 `NAME_None`、含任意空白字符的名称、空回调或重名；锁外绑定 `IConsoleManager`（`ECVF_Cheat`）；引擎已存在同名控制台对象时记 `Warning` 并仍支持 `ExecuteCheat` |
| 注销指令 | `bool UnregisterCheat(FName CommandName)` | 锁外同步卸载绑定的 `IConsoleObject` |
| 执行指令 | `bool ExecuteCheat(FStringView CommandLine)` | 零分配前置命令查找，支持双引号含空格参数、空引号 `""` 及 `\"`/`\\` 转义，锁外安全派发 |
| 查询状态 | `bool HasCheat(FName CommandName) const` / `int32 GetCheatCount() const` | 读锁线程安全查询 |
| 清空全部 | `void ClearAll()` | 卸载所有由注册表创建的控制台命令并清空映射 |

---

## 2. `FGameDebugPrint` (`Print/FGameDebugPrint.h`)

编译期开关：`GAME_ENABLE_DEBUG_DRAW` / `Game::Config::bEnableDebugDraw`。纯静态不可实例化。

- 常量：`static constexpr uint64 InvalidKey = MAX_uint64;`
- `Print(Message, Color = Cyan, Duration = 2.0f, Key = InvalidKey)`：负/非有限 `Duration` 钳制为 `0.0f`；`InvalidKey` 追加新行，其余原地刷新。
- `PrintChannel(Channel, Message, Color = Yellow, Duration = 2.0f)`：经 64 位独立域盐键原地刷新，前缀 `[Channel] `。
- `PrintWarning(Message, Duration = 5.0f)`（橙色）/ `PrintError(Message, Duration = 5.0f)`（红色）。
- `GetChannelKey(FName Channel)`：计算与 `WatchHub` 数学正交的 64 位槽位键，`NAME_None` 返回 `InvalidKey`。
- 宏：`GAME_DEBUG_PRINT`、`GAME_DEBUG_PRINT_COLOR`、`GAME_DEBUG_PRINT_CHANNEL`、`GAME_DEBUG_PRINT_WARN`、`GAME_DEBUG_PRINT_ERROR`（全部 `do { ... } while (false)` 包裹）。

---

## 3. `FGameDebugDraw` (`Visualizer/FGameDebugDraw.h`)

编译期开关：`GAME_ENABLE_DEBUG_DRAW` / `Game::Config::bEnableDebugDraw`。纯静态不可实例化。

提供 9 大图元及对应 `GAME_DEBUG_DRAW_*` 宏：
- `DrawLine`（默认绿）、`DrawSphere`（默认青）、`DrawBox`（默认黄）、`DrawCapsule`（默认绿）、`DrawCylinder`（默认橙）、`DrawCone`（默认黄，弧度制）、`DrawArrow`（默认紫）、`DrawCoordinateSystem`、`DrawString`（默认白）。
- 所有方法自动拦截 `World == nullptr`、`NaN`/`Inf`、负 `Duration`/`Thickness`、非正几何尺度、非法分段数、起终点重合退化与跨世界/失效 `TestActor`，并对 `DrawBox`/`DrawCapsule` 的非单位 `FQuat` 自动安全归一化。

---

## 4. `FGameDebugTrailRecorder` (`Visualizer/FGameDebugTrailRecorder.h`)

编译期开关：`Game::Config::bEnableDebugDraw`（Shipping 跳过环形缓冲堆分配）。值语义定长环形记录器。

- 常量：`static constexpr double UsePlatformClockSentinel = -1.0;`
- `FGameDebugTrailRecorder(InMaxSamples = 64, InLifetimeSeconds = 2.0f, InMinDistance = 10.0f)`：`MaxSamples >= 2`、`LifetimeSeconds >= 0.1f`、`MinDistance >= 0.0f`。
- `bool AddSample(NewLocation, CurrentTimeSeconds = -1.0)`：拦截 `NaN`/非有限时间戳；`CurrentTimeSeconds >= 0.0`（含 `0.0`）直接采用并对回退时间戳做单调非递减钳制；先 $O(1)$ 清理超期点再距离去重与环形写入。
- `void DrawTrail(World, HeadColor = Green, TailColor = Blue, Thickness = 2.0f, Duration = 0.0f, CurrentTimeSeconds = -1.0)`：先淘汰超期样本，前置校验 `Thickness`/`Duration`，再按剩余生命周期时间衰减渐变绘制线段。
- `void Clear() noexcept` / `int32 GetSampleCount() const noexcept` / `const FTrailSample& GetSample(int32 Index) const`。

---

## 5. `FGameDebugTimeController` (`Time/FGameDebugTimeController.h`)

编译期开关：`bEnableGM || bEnableDebugDraw`（GM 指令注册受 `bEnableGM` 控制）。纯静态不可实例化，直接操作 `AWorldSettings::SetTimeDilation` 并按 `UWorld`（`FObjectKey`）隔离状态。

- 常量：`MinTimeDilation = 0.001f`、`MaxTimeDilation = 20.0f`、`PausedTimeDilation = 0.0001f`、`PauseDetectionThreshold = 0.0005f`。
- `Initialize()` / `Shutdown()`：对称注册与注销 `GM.TimeScale`、`GM.TimePause`、`GM.TimeResume`、`GM.TimeReset`、`GM.TimeStep`（严格拒绝尾随杂字符与非正步进帧数）及待执行步进 Ticker。
- `SetTimeDilation` / `GetTimeDilation` / `Pause`（若 `MinGlobalTimeDilation` 阻止降至暂停阈值以下则记录 `Warning` 并返回 `false`）/ `Resume` / `IsPaused` / `ResetTime` / `StepFrame(WorldContextObject, Frames = 1)`（自动补偿 `UWorld::Tick` 中途调用同帧 `CoreTicker`，世界销毁时即时回收状态条目）：传入 `nullptr` 时按活跃 GameWorld 兜底；传入非空无效上下文直接返回失败，绝不污染无关世界。

---

## 6. `FGameDebugWatchHub` (`Watcher/FGameDebugWatchHub.h`)

编译期开关：`GAME_ENABLE_DEBUG_DRAW` / `Game::Config::bEnableDebugDraw`。

- `Initialize()` / `Shutdown()`：挂载/注销 `GM.Watch`（支持 `list` 单锁含项数汇总、`all <on/off/1/0/true/false>`、`<Category> <on/off/1/0/true/false>`，拦截空引号分类名）。
- `bool Watch(Category, Name, Getter)`：同名同分类原地替换闭包；无效参数或禁用开关返回 `false`。
- `bool Unwatch(Category, Name)`：移除观察项；分类变空时即时撤销屏幕面板，处于默认开启态的空分类自动回收。
- `SetCategoryEnabled` / `IsCategoryEnabled` / `SetGlobalEnabled`（关闭时即时撤销全部屏幕面板）/ `IsGlobalEnabled`（原子 `noexcept`）/ `GetCategories` / `GetWatchItemCount` / `ClearAll` / `TickDisplay(DeltaTime)`。
- 宏：`GAME_DEBUG_WATCH(CategoryLiteral, NameLiteral, Getter)`、`GAME_DEBUG_UNWATCH(CategoryLiteral, NameLiteral)`。

---

## 7. `FScopedDebugStopwatch` (`Profiling/FScopedDebugStopwatch.h`)

编译期开关：`GAME_ENABLE_CPU_PROFILER`（屏幕告警叠加 `bEnableDebugDraw`），复用 `GameCore::FScopedCpuTimer`；禁用时裁剪全部成员变量（`sizeof == 1`、零构造/析构时钟开销）。

- 常量：`SevereThresholdMultiplier = 2.0`、`AlertDisplayDurationSeconds = 3.0f`。
- `FScopedDebugStopwatch(InScopeName, InThresholdMs = 0.0, InChannel = "Profiler")` / `double GetElapsedMs() const noexcept`。
- 宏：`GAME_DEBUG_SCOPED_TIMER(ScopeNameLiteral, ThresholdMs)`、`GAME_DEBUG_SCOPED_TIMER_CHANNEL(ScopeNameLiteral, ThresholdMs, ChannelLiteral)`（基于 `__COUNTER__` 防同行冲突）。

---

## 8. `UGameDebugBlueprintLibrary` (`Library/UGameDebugBlueprintLibrary.h`)

向蓝图暴露 `GameDebug|Print`（4 节点）、`GameDebug|Draw`（9 节点，默认色与 `FGameDebugDraw` 完全一致）及 `GameDebug|Time`（7 节点）。
