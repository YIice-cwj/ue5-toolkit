# GameLogging 注册中心 API 参考

`GameLogging` 底层提供了 C++ 原生注册中心类 `FGameLogRegistry` 与 RAII 辅助类 `FModuleLogRegistrar`。当需要绕过宏直接进行底层集成或诊断时，可使用本模块提供的 C++ API。

命名空间：`Game::Log`  
头文件：`#include "Logging/GameLogCategories.h"`

---

## 1. `FGameLogRegistry`

全局线程安全模块日志注册中心。

```cpp
namespace Game::Log {
class GAMELOGGING_API FGameLogRegistry {
public:
    static void RegisterCategory(const char* InModuleName, FLogCategoryBase* InCategory);
    static void UnregisterCategory(const char* InModuleName);
    [[nodiscard]] static FLogCategoryBase& GetCategory(const char* InModuleName);
    [[nodiscard]] static FLogCategoryBase& GetFallbackCategory();
    [[nodiscard]] static int32 GetEpoch() noexcept;
};
}
```

### 方法详细说明

#### `RegisterCategory`
```cpp
static void RegisterCategory(const char* InModuleName, FLogCategoryBase* InCategory);
```
- **职责**：将日志分类指针与模块名称（ANSI 字符串）进行绑定。
- **线程安全与 SIOF 防御**：底层基于内部 `FRegistryStorage::Get()`（Meyers' Singleton 局部静态单例），杜绝跨编译单元全局静态初始化顺序死锁（SIOF）；获取独占写锁（`Storage.Lock`，`SLT_Write`），更新静态映射条目并原子递增全局纪元号（`Storage.Epoch`）。
- **说明**：
  - 模块名按 ANSI 字符串深拷贝入内部数组，调用方临时缓冲区销毁后仍安全；
  - 若模块已存在，覆写其分类指针；
  - 新模块直接追加（`TArray` 动态容量，无固定条目上限）；
  - 每次成功更新原子递增全局 `Epoch`，触发调用点缓存失效。

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
- **返回值**：返回引擎全局导出的 `LogGame` 引用。

#### `GetEpoch`
```cpp
[[nodiscard]] static int32 GetEpoch() noexcept;
```
- **职责**：获取当前注册表变更纪元号（`std::memory_order_relaxed` 原子读取）。
- **说明**：调用点缓存可通过此纪元号判断全局映射是否发生了增删，以实现缓存失效校验。

---

## 2. `FModuleLogRegistrar`

RAII 自动注册守卫。

```cpp
namespace Game::Log {
class GAMELOGGING_API FModuleLogRegistrar {
public:
    FModuleLogRegistrar(const char* InModuleName, FLogCategoryBase* InCategory);
    ~FModuleLogRegistrar();
private:
    TArray<ANSICHAR> ModuleName;
};
}
```

### 构造与析构语义
- **构造函数**：自动调用 `FGameLogRegistry::RegisterCategory(InModuleName, InCategory)`。
- **析构函数**：自动调用 `FGameLogRegistry::UnregisterCategory(ModuleName)`。
- **用途**：供 `GAME_REGISTER_MODULE_LOG` 宏在全局作用域实例化静态常量对象，使得模块加载时自动注册、模块卸载时自动注销，无需任何生命周期手动管理代码。

