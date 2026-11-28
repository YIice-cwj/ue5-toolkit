# GameLogging 注册中心 API 参考

> **一句话**：`FGameLogRegistry` 以进程常驻原子槽位维护模块名到日志分类的映射，`FModuleLogRegistrar` 提供 RAII 登记与恢复。
> **头文件**：`Logging/FGameLogRegistry.h`、`Logging/FModuleLogRegistrar.h`（或 `GameLogging.h`）
> **线程**：全部公开入口线程安全；槽位为地址恒定的 `std::atomic<FLogCategoryBase*>`
> **失败语义**：无错误码；空模块名或空分类被忽略，未注册模块返回兜底分类 `LogGame`

## 30 秒速览

- **单例门面**：`FGameLogRegistry` 是纯静态工具类，禁止构造实例，提供 5 个静态入口。
- **槽位**：`FLogCategorySlot = std::atomic<FLogCategoryBase*>`，进程期地址恒定，`ResolveCategorySlot` 永不返回 `nullptr`。
- **登记**：`RegisterCategory` 以区分大小写的 ANSI 模块名做 O(1) 哈希索引并原子更新槽位。
- **RAII**：`FModuleLogRegistrar` 构造即登记，析构 O(1) 恢复前一项分类或回退兜底分类。
- **约束**：同一模块的多个注册器实例必须按 LIFO 顺序析构；空入参不登记。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `FGameLogRegistry::RegisterCategory` | 登记或按模块名覆写分类 | 线程安全；空模块名或空分类忽略 |
| `FGameLogRegistry::UnregisterCategory` | 注销模块并回退兜底分类 | 空模块名忽略 |
| `FGameLogRegistry::GetCategory` | 按模块名查分类 | 区分大小写 O(1)；未注册或空名返回兜底分类 |
| `FGameLogRegistry::GetFallbackCategory` | 取兜底分类 | `NO_LOGGING` 构建返回局部静态实例 |
| `FGameLogRegistry::ResolveCategorySlot` | 解析或按需创建常驻槽位 | 永不返回 `nullptr`；指针与进程同寿命 |
| `FModuleLogRegistrar`（构造） | RAII 登记并缓存覆盖前分类 | 空模块名或空分类时不登记 |
| `FModuleLogRegistrar`（析构） | O(1) 恢复前一项分类或回退兜底 | 同模块实例必须 LIFO 顺序析构 |

## `FGameLogRegistry`

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

| 方法 | 参数 | 返回 | 行为 |
| :--- | :--- | :--- | :--- |
| `RegisterCategory` | `InModuleName`：模块名；`InCategory`：分类指针 | `void` | 登记或按模块名覆写分类，并原子更新该模块槽位；空模块名或空分类忽略 |
| `UnregisterCategory` | `InModuleName`：模块名 | `void` | 注销指定模块，槽位原子回退至 `GetFallbackCategory()`；空模块名忽略 |
| `GetCategory` | `InModuleName`：模块名 | `FLogCategoryBase&` | 按区分大小写模块名 O(1) 查找；未注册或空名返回兜底分类 |
| `GetFallbackCategory` | 无 | `FLogCategoryBase&` | 常规构建返回全局 `LogGame`；`NO_LOGGING` 构建返回局部静态 `FLogCategoryBase`（分类名 `LogGame`） |
| `ResolveCategorySlot` | `InModuleName`：模块名 | `const FLogCategorySlot*` | 解析或按需创建常驻槽位；空模块名返回恒定指向兜底分类的槽位；永不返回 `nullptr` |

- **线程**：内部使用 `FRWLock`；注册 / 注销取写锁，查询取读锁，槽位读写为原子操作。
- **生命周期**：槽位记录由 `TUniquePtr` 持有，容器扩容不移动槽位地址；注册中心在进程退出时不析构。
- **失败语义**：入参无效时静默忽略或返回兜底分类，不抛异常；未注册模块的日志不丢失，统一落到 `LogGame`。

```cpp
#include "Logging/FGameLogRegistry.h"
#include "Logging/GameLogCategories.h"

Game::Log::FGameLogRegistry::RegisterCategory("MyModule", &LogGame);
Game::Log::FLogCategoryBase& Active = Game::Log::FGameLogRegistry::GetCategory("MyModule");
const Game::Log::FLogCategorySlot* Slot = Game::Log::FGameLogRegistry::ResolveCategorySlot("MyModule");
Game::Log::FGameLogRegistry::UnregisterCategory("MyModule");
```

## `FModuleLogRegistrar`

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
};

} // namespace Game::Log
```

| 入口 | 参数 | 行为 |
| :--- | :--- | :--- |
| 构造函数 | `InModuleName`：非空模块名；`InCategory`：分类指针 | 立即登记，并缓存槽位记录与覆盖前分类；模块名为空指针 / 空串或分类为空时不登记 |
| 析构函数 | 无 | O(1) 无需查表；仅当当前槽位仍归本实例所有时恢复 `PreviousCategory` 或回退兜底分类 |

- **线程**：构造与析构线程安全；仍须遵守同一模块实例的 LIFO 析构顺序。
- **生命周期**：典型用法是文件作用域静态对象；不要堆分配或打乱嵌套顺序。
- **失败语义**：无效入参产生空操作对象，析构时安全无操作。

```cpp
{
    Game::Log::FModuleLogRegistrar ScopedRegistrar("MyModule", &LogGame);
    Game::Log::FLogCategoryBase& Active = Game::Log::FGameLogRegistry::GetCategory("MyModule");
}
```

## 常见错误与边界

- **大小写敏感**：模块名是区分大小写的 ANSI 字符串，`"MyModule"` 与 `"mymodule"` 是不同槽位。
- **空名忽略**：`RegisterCategory` / `UnregisterCategory` / `FModuleLogRegistrar` 对空模块名或空分类不产生登记。
- **LIFO 约束**：同一模块多个注册器必须后进先出析构，否则较早实例析构时可能覆盖后续登记。
- **槽位寿命**：`ResolveCategorySlot` 返回的指针与进程同寿命，调用方可永久缓存，但不得释放或改写。
- **外部覆写**：若槽位已被其他实例注销或覆写，RAII 析构会检测归属并安全跳过。
- **`NO_LOGGING`**：兜底分类是局部静态实例而非全局 `LogGame`，但分类名保持一致。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [快速上手指南](../Guides/QuickStart.md)
