# GameLogging 宏参考手册

本文档列出 `GameLogging` 插件导出的全部核心日志宏及其调用语义与参数说明。

---

## 1. 注册宏

### 1.1 `GAME_REGISTER_MODULE_LOG(CategoryName)`
在模块的主实现文件（通常为 `Private/MyModule.cpp`）最顶层调用，用于声明静态日志分类并利用 RAII 自动向全局注册中心登记。

- **参数**：
  - `CategoryName`：分类标识符（如 `LogGameCore`、`LogCombatSystem`）。
- **示例**：
  ```cpp
  // 在 Module.cpp 顶部
  GAME_REGISTER_MODULE_LOG(LogCombatSystem);
  ```

### 1.2 `GAME_REGISTER_MODULE_CATEGORY(CategoryRef)`
用于将外部已声明导出（`DECLARE_LOG_CATEGORY_EXTERN`）的现有分类绑定到当前模块名。

- **参数**：
  - `CategoryRef`：已存在的 `FLogCategoryBase` 引用。

---

## 2. 运行时日志宏

### 2.1 `GAME_LOG(Verbosity, Format, ...)`
自动按当前所属模块分发的通用日志宏。

- **参数**：
  - `Verbosity`：日志冗余级别，支持 `Fatal`, `Error`, `Warning`, `Display`, `Log`, `Verbose`, `VeryVerbose`。
  - `Format`：格式化字符串（如 `TEXT("Value: %d")`）。
  - `...`：可变参数列表。
- **说明**：
  - 若调用点所在文件显式定义了 `GAME_CURRENT_MODULE_LOG`，直接内联为原生 `UE_LOG`。
  - 否则自动匹配当前 UBT 模块名分类；若未注册则自动输出到 `LogGame`。
- **示例**：
  ```cpp
  GAME_LOG(Log, TEXT("Player %s spawned at %s"), *PlayerName, *Location.ToString());
  ```

---

### 2.2 `GAME_CLOG(Condition, Verbosity, Format, ...)`
条件日志宏：仅在 `Condition` 为真时执行日志格式化与输出。

- **参数**：
  - `Condition`：布尔表达式。
  - `Verbosity` / `Format` / `...`：同 `GAME_LOG`。
- **示例**：
  ```cpp
  GAME_CLOG(Health <= 0.0f, Warning, TEXT("Entity %s health dropped to 0!"), *GetName());
  ```

---

### 2.3 `GAME_LOG_ONCE(Verbosity, Format, ...)`
单次日志宏：在应用程序整个生命周期内，该代码行仅输出首次日志，后续调用自动忽略。

- **特点**：
  - 内部基于局部 `static std::atomic<bool>` 与 `exchange(true)` 实现。
  - 线程安全、绝对无锁，适合用于已知容错路径、废弃接口警告、一次性初始化提示。
- **示例**：
  ```cpp
  void OldApi()
  {
      GAME_LOG_ONCE(Warning, TEXT("OldApi is deprecated, please migrate to NewApi!"));
      // ...
  }
  ```

---

### 2.4 `GAME_CLOG_ONCE(Condition, Verbosity, Format, ...)`
条件单次日志宏：仅在 `Condition` 首次为真时输出一次日志。

- **参数**：
  - `Condition`：布尔表达式。
- **示例**：
  ```cpp
  GAME_CLOG_ONCE(Items.Num() > 1000, Warning, TEXT("Inventory exceeded 1000 items!"));
  ```

---

### 2.5 `GAME_LOG_THROTTLE(SecondsInterval, Verbosity, Format, ...)`
限频日志宏：限制该调用点最多每隔指定秒数输出一次，防止高频 Tick / 循环内造成控制台或文件刷屏。

- **特点**：
  - 内部基于局部 `static std::atomic<double>` 与 `FPlatformTime::Seconds()` 协同工作。
  - **首触即打保证**：时间戳初始值赋为负哨兵（`-1.0e9`），确保无论游戏启动时钟多小，首次触达该调用点时必立即输出，绝无首帧吞日志问题。
  - **无锁并发更新**：采用 CAS (`compare_exchange_strong`) 原子操作，多线程竞争下仅胜出者更新时间戳并输出，并发安全且零锁开销。
- **参数**：
  - `SecondsInterval`：最小输出间隔（秒，类型为 `double` 或 `float`，如 `0.5`, `2.0`）。
  - `Verbosity` / `Format` / `...`：同 `GAME_LOG`。
- **示例**：
  ```cpp
  void AAIController::Tick(float DeltaTime)
  {
      Super::Tick(DeltaTime);

      // 首次立即输出，后续每隔 1.5 秒输出一次 AI 状态追踪
      GAME_LOG_THROTTLE(1.5, Verbose, TEXT("AI [%s] target distance: %.2f"), *GetName(), TargetDist);
  }
  ```

---

## 3. 宏汇总表

| 宏名称 | 是否带条件 | 生命周期次数 | 频率限制 | 典型应用场景 |
| :--- | :--- | :--- | :--- | :--- |
| `GAME_LOG` | 否 | 无限 | 无 | 常规流程、状态流转、单次触发事件 |
| `GAME_CLOG` | 是 | 无限 | 无 | 边界异常判定、容错降级分支 |
| `GAME_LOG_ONCE` | 否 | 严格 1 次 | 1 次/生命周期 | 废弃接口提醒、驱动兼容检查 |
| `GAME_CLOG_ONCE` | 是 | 严格 1 次 | 1 次/生命周期 | 首度越界警告、单次资源耗尽提醒 |
| `GAME_LOG_THROTTLE` | 否 | 无限 | $\ge T$ 秒 | Tick 调试、网络同步追踪、高频循环监测 |

