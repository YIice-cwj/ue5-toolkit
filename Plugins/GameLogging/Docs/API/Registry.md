# GameLogging 注册中心 API 参考

`GameLogging` 底层提供了 C++ 原生注册中心类 `FGameLogRegistry` 与 RAII 辅助类 `FModuleLogRegistrar`。当需要绕过宏直接进行底层集成或诊断时，可使用本模块提供的 C++ API。

命名空间：`Game::Log`  
头文件：`#include "Logging/FGameLogRegistry.h"`（或全局引入 `#include "Logging/GameLogCategories.h"`）

---

## 1. `FGameLogRegistry`

全局线程安全模块日志注册中心。

```cpp
namespace Game::Log {
class GAMELOGGING_API FGameLogRegistry final {
public:
    FGameLogRegistry() = delete;
    static void RegisterCategory(const char* InModuleName, FLogCategoryBase* InCategory);
    static void UnregisterCategory(const char* InModuleName);
    [[nodiscard]] static FLogCategoryBase& GetCategory(const char* InModuleName);
    [[nodiscard]] static FLogCategoryBase& GetFallbackCategory();
    [[nodiscard]] static int32 GetEpoch() noexcept;

private:
    friend class FModuleLogRegistrar;
    [[nodiscard]] static TArray<ANSICHAR> MakeAnsiName(const char* InName);
    [[nodiscard]] static FLogCategoryBase* RegisterScopedCategory(const char* InModuleName, FLogCategoryBase* InCategory);
    static void UnregisterScopedCategory(const char* InModuleName, FLogCategoryBase* InCategory,
        FLogCategoryBase* InPreviousCategory);
};
}
```

### 方法详细说明

#### `RegisterCategory`
```cpp
static void RegisterCategory(const char* InModuleName, FLogCategoryBase* InCategory);
```
- **职责**：将日志分类指针与模块名称（ANSI 字符串）进行绑定。
- **线程安全与 SIOF 防御**：底层基于内部 `FRegistryStorage::Get()` 常驻指针单例，按需延迟初始化根除跨编译单元静态初始化顺序死锁（SIOF），且常驻进程全生命周期彻底避免程序退出期静态逆序析构（Use-After-Destruction）崩溃；获取独占写锁（`Storage.Lock`，`SLT_Write`），更新静态映射条目并原子递增全局纪元号（`Storage.Epoch`）。
- **说明**：
  - 模块名经内部静态方法 `MakeAnsiName` 按 ANSI 字符串深拷贝入自持数组，调用方临时缓冲区销毁后仍安全；
  - 若模块已存在且分类指针改变，覆写其分类指针并原子递增全局 `Epoch`；
  - **幂等保护**：若模块已绑定相同分类指针，直接短路返回，不推进 `Epoch`，避免无谓触发全局调用点局部缓存失效；
  - 新模块直接追加（`TArray` 动态容量，无固定条目上限）；
  - 任何实际产生映射变更的注册与覆写均原子递增全局 `Epoch`，驱动调用点缓存自愈刷新。

#### `UnregisterCategory`
```cpp
static void UnregisterCategory(const char* InModuleName);
```
- **职责**：注销指定模块名称绑定的日志分类。
- **说明**：通常在模块卸载或 RAII 析构时调用，在写锁保护下移除条目并原子递增全局纪元号。

#### `GetCategory`
```cpp
[[nodiscard]] static FLogCategoryBase& GetCategory(const char* InModuleName);
```
- **职责**：根据模块名称查找对应的日志分类。
- **线程安全**：内部获取全局读锁（`SLT_ReadOnly`）。
- **返回值**：若找到且指针有效，返回注册的分类；若未找到或入参为空，返回 `GetFallbackCategory()`。

#### `GetFallbackCategory`
```cpp
[[nodiscard]] static FLogCategoryBase& GetFallbackCategory();
```
- **职责**：获取未注册或默认全局兜底分类。
- **返回值**：在常规构建下返回引擎全局导出的 `LogGame` 引用；在 `NO_LOGGING` 宏生效下返回局部静态 `NoLoggingFallback` 引用。

#### `GetEpoch`
```cpp
[[nodiscard]] static int32 GetEpoch() noexcept;
```
- **职责**：获取当前注册表变更纪元号（`std::memory_order_acquire` 原子读取，与写侧 `release` 递增配对）。
- **说明**：调用点缓存可通过此纪元号判断全局映射是否发生了增删，以实现缓存失效校验。

---

## 2. `FModuleLogRegistrar`

RAII 自动注册守卫。

```cpp
namespace Game::Log {
class GAMELOGGING_API FModuleLogRegistrar final {
public:
    FModuleLogRegistrar(const char* InModuleName, FLogCategoryBase* InCategory);
    ~FModuleLogRegistrar();

    FModuleLogRegistrar(const FModuleLogRegistrar&) = delete;
    FModuleLogRegistrar& operator=(const FModuleLogRegistrar&) = delete;
    FModuleLogRegistrar(FModuleLogRegistrar&&) = delete;
    FModuleLogRegistrar& operator=(FModuleLogRegistrar&&) = delete;

private:
    TArray<ANSICHAR> ModuleName;
    FLogCategoryBase* Category = nullptr;
    FLogCategoryBase* PreviousCategory = nullptr;
};
}
```

### 构造与析构语义
- **构造函数**：注册新分类并保存被覆盖的前一项分类；模块名为空或分类为空时不注册。
- **析构函数**：仅当当前登记仍归本实例所有时，恢复构造前的前一项分类；此前没有登记则执行注销。
- **生命周期约束**：同一模块的多个实例必须处于严格嵌套作用域并按 LIFO 顺序销毁，不支持堆分配后的非嵌套或乱序销毁。
- **用途**：供 `GAME_REGISTER_MODULE_LOG` 宏在全局作用域实例化静态常量对象，使得模块加载时自动注册、模块卸载时自动注销，无需任何生命周期手动管理代码。

