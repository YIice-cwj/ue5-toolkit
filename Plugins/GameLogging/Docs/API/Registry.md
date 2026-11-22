# GameLogging 注册中心 API 参考

`GameLogging` 底层提供了 C++ 原生注册中心类 `FGameLogRegistry` 与 RAII 辅助类 `FModuleLogRegistrar`。当需要绕过宏直接进行底层集成或诊断时，可使用本模块提供的 C++ API。

命名空间：`Game::Log`  
头文件：`#include "Logging/FGameLogRegistry.h"`（或 `#include "Logging/GameLogCategories.h"`）

---

## 1. `FGameLogRegistry`

全局线程安全模块日志注册中心。

```cpp
namespace Game::Log {

using FLogCategorySlot = std::atomic<FLogCategoryBase*>;
struct FModuleLogSlotRecord;

class GAMELOGGING_API FGameLogRegistry final {
public:
    FGameLogRegistry() = delete;
    static void RegisterCategory(const char* InModuleName, FLogCategoryBase* InCategory);
    static void UnregisterCategory(const char* InModuleName);
    [[nodiscard]] static FLogCategoryBase& GetCategory(const char* InModuleName);
    [[nodiscard]] static FLogCategoryBase& GetFallbackCategory();
    [[nodiscard]] static const FLogCategorySlot* ResolveCategorySlot(const char* InModuleName);
};

} // namespace Game::Log
```

### 方法详细说明

#### `RegisterCategory`
```cpp
static void RegisterCategory(const char* InModuleName, FLogCategoryBase* InCategory);
```
- **职责**：将日志分类指针与区分大小写的 ANSI 模块名称进行绑定，并原子更新该模块的常驻槽位。
- **实现机制**：
  - 底层基于静态缓冲常驻单例 `FRegistryStorage::Get()`（C++20 `std::construct_at` 线程安全按需构造、退出不析构，零裸 `new`/`delete`、彻底免疫 SIOF）；
  - 通过自定义 `TMap` + 单趟 FNV-1a 哈希视图实现 $O(1)$ 零临时分配查找；模块名深拷贝至键内自持；
  - 槽位记录通过 `TUniquePtr<FModuleLogSlotRecord>` 持有，地址永不随容器扩容移动；写入新分类时仅原子更新该模块的 `FLogCategorySlot`，不干扰任何其他模块的缓存。

#### `UnregisterCategory`
```cpp
static void UnregisterCategory(const char* InModuleName);
```
- **职责**：注销指定模块名称绑定的日志分类，并将其常驻槽位原子回退至 `GetFallbackCategory()`。

#### `GetCategory`
```cpp
[[nodiscard]] static FLogCategoryBase& GetCategory(const char* InModuleName);
```
- **职责**：按区分大小写的 ANSI 模块名 $O(1)$ 查找当前生效的日志分类；未注册或入参为空时返回 `GetFallbackCategory()`。

#### `GetFallbackCategory`
```cpp
[[nodiscard]] static FLogCategoryBase& GetFallbackCategory();
```
- **职责**：获取全局兜底分类；常规构建返回 `LogGame`，`NO_LOGGING` 构建返回局部静态 `NoLoggingFallback`。

#### `ResolveCategorySlot`
```cpp
[[nodiscard]] static const FLogCategorySlot* ResolveCategorySlot(const char* InModuleName);
```
- **职责**：解析或按需创建指定模块对应的常驻只读原子槽位指针（永不为 `nullptr`）。空模块名返回恒指向兜底分类的常驻槽位。
- **用途**：供 `GAME_LOG` / `GAME_LOG_THROTTLE` 调用点首次解析后永久缓存，热路径直接 `Slot->load(std::memory_order_acquire)`，零跨 DLL 调用与零锁竞争。

---

## 2. `FModuleLogRegistrar`

RAII 自动注册守卫，严格嵌套作用域退出时恢复前一项登记。

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
    FModuleLogSlotRecord* SlotRecord = nullptr;
    FLogCategoryBase* Category = nullptr;
    FLogCategoryBase* PreviousCategory = nullptr;
};
}
```

### 构造与析构语义
- **构造函数**：直接登记分类并缓存不透明槽位记录指针 `SlotRecord` 与覆盖前分类 `PreviousCategory`（无需重复拷贝模块名字符串）；无效入参不注册。
- **析构函数**：$O(1)$ 无需查表，仅当当前槽位仍处于已注册状态且归本实例所有时恢复 `PreviousCategory` 或回退兜底分类；若已被外部覆写或提前注销则安全无操作。
- **生命周期约束**：同一模块的多个实例必须按 LIFO 顺序嵌套销毁。
