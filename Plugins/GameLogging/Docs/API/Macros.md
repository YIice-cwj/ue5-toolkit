# GameLogging 宏参考手册

本文档列出 `GameLogging` 插件导出的全部核心日志宏及其调用语义与参数说明。

头文件：`#include "Logging/GameLogMacros.h"`（或 `#include "Logging/GameLogCategories.h"` / `#include "GameLogging.h"`）

---

## 1. 注册宏

### 1.1 `GAME_REGISTER_MODULE_LOG(CategoryName)`
在模块的主实现文件（通常为 `Private/MyModule.cpp`）最顶层调用，用于声明静态日志分类并利用 RAII 自动向全局注册中心登记。内部使用 `__COUNTER__` 生成唯一注册器符号，完全避免 Unity Build 行号碰撞。

- **参数**：
  - `CategoryName`：分类标识符（如 `LogGameCore`、`LogCombatSystem`）。
- **示例**：
  ```cpp
  GAME_REGISTER_MODULE_LOG(LogCombatSystem);
  ```

### 1.2 `GAME_REGISTER_MODULE_CATEGORY(CategoryRef)`
用于将外部已声明导出（`DECLARE_LOG_CATEGORY_EXTERN`）的现有分类绑定到当前模块名。

- **参数**：
  - `CategoryRef`：已存在的 `FLogCategoryBase` 引用。

---

## 2. 运行时日志宏

所有宏内部逻辑均收拢至 `Game::Log::Private` `FORCEINLINE` 辅助函数，并将宏作用域内的极少数局部标识符统一冠以 `GameLogPrivate` 前缀，彻底杜绝遮蔽调用方同名局部变量（如 `CurrentSeconds`、`LastSeconds`）。

### 2.1 `GAME_LOG(Verbosity, Format, ...)`
自动按当前所属模块分发的通用日志宏。

- **参数**：
  - `Verbosity`：日志冗余级别，支持 `Fatal`, `Error`, `Warning`, `Display`, `Log`, `Verbose`, `VeryVerbose`。
  - `Format`：格式化字符串（如 `TEXT("Value: %d")`）。
  - `...`：可变参数列表。
- **说明**：
  - 若调用点所在文件显式定义了 `GAME_CURRENT_MODULE_LOG`，直接内联为原生 `UE_LOG`。
  - 否则首次调用通过 `FGameLogRegistry::ResolveCategorySlot(UE_MODULE_NAME)` 取得模块常驻原子槽位指针并永久缓存；后续热路径仅两次无锁原子读，零跨 DLL 调用、零锁竞争，且注册/覆写/注销即时无撕裂可见。

---

### 2.2 `GAME_CLOG(Condition, Verbosity, Format, ...)`
条件日志宏：仅在 `Condition` 为真时执行日志格式化与输出。

---

### 2.3 `GAME_LOG_ONCE(Verbosity, Format, ...)`
单次日志宏：在应用程序整个生命周期内，该代码行仅输出首次日志，后续调用自动忽略。

- **特点**：
  - 基于局部 `static std::atomic<bool>` 与 `Game::Log::Private::TryConsumeOnce`（TTAS 无锁双重检测）；
  - 触发后通过 `relaxed` 只读检查极速短路，避免重复写入导致 CPU 缓存行颠簸。

---

### 2.4 `GAME_CLOG_ONCE(Condition, Verbosity, Format, ...)`
条件单次日志宏：仅在 `Condition` 首次为真时输出一次日志。

- **特点**：
  - 采用短路求值链 `!bHasLogged.load(relaxed) && (Condition) && !bHasLogged.exchange(true, relaxed)`；
  - `Condition` 为假时不消耗单次机会；首次为真触发后，后续调用直接跳过 `Condition` 求值，零额外计算开销。

---

### 2.5 `GAME_LOG_THROTTLE(SecondsInterval, Verbosity, Format, ...)`
限频日志宏：限制该调用点最多每隔指定秒数输出一次，防止高频 Tick / 循环内造成控制台或文件刷屏。

- **特点**：
  - **抑制前置短路**：先检查目标日志分类在当前 `Verbosity` 下是否被抑制；被抑制时直接跳过 `FPlatformTime::Seconds()` 与 CAS，既消除高频 Tick 时钟损耗，又避免抑制期间白白消耗节流窗口（运行期放开级别后立刻首触即打）。
  - **首触即打保证**：时间戳初值为负哨兵 `-1.0e9`，未抑制状态下首次触达必定立即输出。
  - **无锁并发更新**：内部通过 `Game::Log::Private::TryConsumeThrottle`（CAS `compare_exchange_strong`）保证多线程并发下仅单一胜出者输出。

---

## 3. 宏汇总表

| 宏名称 | 是否带条件 | 生命周期次数 | 频率限制 | 典型应用场景 |
| :--- | :--- | :--- | :--- | :--- |
| `GAME_LOG` | 否 | 无限 | 无 | 常规流程、状态流转、单次触发事件 |
| `GAME_CLOG` | 是 | 无限 | 无 | 边界异常判定、容错降级分支 |
| `GAME_LOG_ONCE` | 否 | 严格 1 次 | 1 次/生命周期 | 废弃接口提醒、一次性初始化提示 |
| `GAME_CLOG_ONCE` | 是 | 严格 1 次 | 1 次/生命周期 | 首度越界警告、单次资源耗尽提醒 |
| `GAME_LOG_THROTTLE` | 否 | 无限 | $\ge T$ 秒 | Tick 调试、网络同步追踪、高频循环监测 |
