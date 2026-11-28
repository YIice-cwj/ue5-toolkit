# GameLogging 日志宏 API 参考

> **一句话**：模块感知的日志宏，提供编译期直轨与常驻原子槽位分发、条件 / 单次 / 限频输出。
> **头文件**：`Logging/GameLogMacros.h`（或 `Logging/GameLogCategories.h` / `GameLogging.h`）
> **线程**：任意线程可调用；槽位原子读写、TTAS 与 CAS 均无锁
> **失败语义**：宏无返回值；`Game::Config::bEnableLog == false` 或分类被抑制时不输出，不抛异常

## 30 秒速览

- **注册**：`GAME_REGISTER_MODULE_LOG` 在模块主 `.cpp` 顶层声明并登记分类；`GAME_REGISTER_MODULE_CATEGORY` 绑定已声明或导出的分类。
- **分发**：定义 `GAME_CURRENT_MODULE_LOG` 时直连原生 `UE_LOG`；否则调用点首次 `ResolveCategorySlot(UE_MODULE_NAME)` 后永久缓存槽位。
- **宏族**：`GAME_LOG`、`GAME_CLOG`、`GAME_LOG_ONCE`、`GAME_CLOG_ONCE`、`GAME_LOG_THROTTLE`。
- **限频差异**：`GAME_LOG_THROTTLE` 先判断分类抑制，再读时钟与执行 CAS；抑制期间不消耗节流窗口，未抑制时首次必打。
- **裁剪**：所有日志宏包裹 `if constexpr (Game::Config::bEnableLog)`；`NO_LOGGING` 下注册宏为空实现，日志整体编译移除。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `GAME_REGISTER_MODULE_LOG` | 声明并登记模块日志分类 | 仅能放在模块主 `.cpp` 顶层；`NO_LOGGING` 下展开为空实现 |
| `GAME_REGISTER_MODULE_CATEGORY` | 绑定已声明或导出的分类 | 只做绑定，不重复定义分类 |
| `GAME_LOG` | 常规日志输出 | 任意线程可调用；未注册模块回落兜底分类 `LogGame` |
| `GAME_CLOG` | 条件日志输出 | `Condition` 仅在需要输出时求值；`bEnableLog == false` 时不求值 |
| `GAME_LOG_ONCE` | 调用点级单次日志 | 去重标记是调用点静态变量，非全局或按文本去重 |
| `GAME_CLOG_ONCE` | 条件单次日志 | `Condition` 为假时不消耗单次机会 |
| `GAME_LOG_THROTTLE` | 限频日志输出 | 抑制期间不消耗节流窗口；未抑制时首次必打；最小间隔非精确周期 |

## 注册宏

```cpp
#define GAME_REGISTER_MODULE_LOG(CategoryName)
#define GAME_REGISTER_MODULE_CATEGORY(CategoryRef)
```

| 宏 | 参数 | 行为 |
| :--- | :--- | :--- |
| `GAME_REGISTER_MODULE_LOG` | `CategoryName`：分类标识符（如 `LogCombatSystem`） | 展开为 `DEFINE_LOG_CATEGORY_STATIC(CategoryName, Log, All)` 与静态 `FModuleLogRegistrar`，以 `UE_MODULE_NAME` 登记，并用 `__COUNTER__` 生成唯一符号 |
| `GAME_REGISTER_MODULE_CATEGORY` | `CategoryRef`：已声明 / 导出的 `FLogCategoryBase` 引用 | 以 `&(CategoryRef)` 绑定当前模块名，不重复定义分类 |

- **线程**：宏展开出的静态注册器在静态初始化期构造，必须放在模块主 `.cpp` 顶层。
- **生命周期**：注册器为静态对象，进程期有效；`NO_LOGGING` 下两个宏展开为空，分类符号不生成。
- **失败语义**：无返回值；不登记时日志回落兜底分类 `LogGame`，分类标识符非法则在编译期失败。

```cpp
#include "Logging/GameLogMacros.h"

GAME_REGISTER_MODULE_LOG(LogMyModule);
```

## 运行时日志宏

```cpp
#define GAME_LOG(Verbosity, Format, ...)
#define GAME_CLOG(Condition, Verbosity, Format, ...)
#define GAME_LOG_ONCE(Verbosity, Format, ...)
#define GAME_CLOG_ONCE(Condition, Verbosity, Format, ...)
#define GAME_LOG_THROTTLE(SecondsInterval, Verbosity, Format, ...)
```

| 参数 | 说明 |
| :--- | :--- |
| `Condition` | 仅在需要输出时求值的判断表达式（`GAME_CLOG*`） |
| `Verbosity` | `Fatal` / `Error` / `Warning` / `Display` / `Log` / `Verbose` / `VeryVerbose` |
| `SecondsInterval` | 最小输出间隔（秒），如 `2.0` |
| `Format` / `...` | `TEXT(...)` 格式化字符串与可变参数 |

### `GAME_LOG`

- 若当前文件显式定义 `GAME_CURRENT_MODULE_LOG`，直接内联为原生 `UE_LOG`。
- 否则通过调用点静态 `std::atomic<const FLogCategorySlot*>` 缓存模块常驻槽位；热路径仅两次原子读，零跨 DLL 调用、零锁竞争。
- 注册、覆写与注销对同一调用点即时可见。

### `GAME_CLOG`

- 仅在 `Condition` 为真时格式化并输出；`bEnableLog == false` 时 `Condition` 不求值。

### `GAME_LOG_ONCE`

- 基于调用点局部 `static std::atomic<bool>` 与 `Game::Log::Private::TryConsumeOnce`（TTAS 双重检测）。
- 首次胜出线程输出一次；触发后仅做 `relaxed` 只读短路。
- 单次语义是**调用点级**：每个宏展开位置各自独立。

### `GAME_CLOG_ONCE`

- 短路求值链：`!bHasLogged.load(relaxed) && (Condition) && !bHasLogged.exchange(true, relaxed)`。
- `Condition` 为假时不消耗单次机会；首次为真触发后，后续调用不再求值 `Condition`。

### `GAME_LOG_THROTTLE`

- **抑制前置短路**：先检查目标分类在当前 `Verbosity` 下是否被抑制；被抑制时跳过 `FPlatformTime::Seconds()` 与 CAS，且不消耗节流窗口。
- **首触即打**：时间戳初值为负哨兵 `-1.0e9`，未抑制状态下首次触达必定输出。
- **无锁并发**：`TryConsumeThrottle` 以 `compare_exchange_strong` 保证多线程并发下仅单一胜出者输出。
- 限频间隔是最小间隔，不是精确周期；高并发下部分调用会被丢弃。

```cpp
void UMySubsystem::ProcessEvent(int32 EventId) {
    GAME_LOG(Log, TEXT("Processing Event ID: %d"), EventId);
    GAME_CLOG(EventId < 0, Error, TEXT("Invalid negative EventId: %d"), EventId);
    GAME_CLOG_ONCE(EventId == 999, Warning, TEXT("EventId 999 is reserved"));
}

void AMyActor::Tick(float DeltaTime) {
    GAME_LOG_THROTTLE(2.0, Verbose, TEXT("Actor location: %s"), *GetActorLocation().ToString());
}
```

## 常见错误与边界

- **注册位置**：`GAME_REGISTER_MODULE_LOG` 只能放在模块主 `.cpp` 顶层；放进头文件会随每个翻译单元生成静态分类。
- **未注册模块**：日志仍会输出到兜底分类 `LogGame`；之后调用 `RegisterCategory` 会通过同一槽位即时生效。
- **`ONCE` 粒度**：去重标记是调用点静态变量，不是全局或按消息文本去重。
- **条件副作用**：`GAME_CLOG*` 的 `Condition` 在日志被编译裁剪或短路后不会求值，不要在其中放必需副作用。
- **限频并发**：CAS 只允许一个线程胜出，不能用它做精确计数或周期性任务调度。
- **Shipping**：`NO_LOGGING == 1` 时 `GAME_ENABLE_LOG` 默认为 `0`，`GAME_LOG*` 分支被静态裁剪，实参不参与运行期求值。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [快速上手指南](../Guides/QuickStart.md)
- [最佳实践与性能指南](../Guides/BestPractices.md)
