# GameCore 配置注册中心 API 参考

`FConfigRegistry` 提供了全生命周期、线程安全、类型安全的动态配置中心。上层业务、调试开关、网络环境等配置均可通过其集中注册与管理。

头文件：`#include "Config/FConfigRegistry.h"`  
蓝图库：`#include "Config/UConfigBPLibrary.h"`

---

## 1. 核心数据类型

### 1.1 `FConfigVariant`
配置存储的基础类型擦除变体，支持标量与引擎常用数学类型：
```cpp
namespace Game::Config {
using FConfigVariant = TVariant<
    bool,
    int32,
    int64,
    float,
    double,
    FString,
    FName,
    FVector,
    FRotator,
    FGameplayTag
>;
}
```
成员上限 24 字节，强类型严格匹配，杜绝隐式数值截断。

### 1.2 `ESetConfigResult`
配置覆盖写入的结果码：
```cpp
enum class ESetConfigResult : uint8 {
    Applied,          // 写入成功（包含首次注册与运行时覆盖）
    RejectedReadOnly, // 写入被拒绝（目标键为只读，如 Env.* 或 Shipping 下的 Dev.* 键）
};
```

---

## 2. `FConfigRegistry` 类方法

```cpp
class GAMECORE_API FConfigRegistry {
public:
    using FConfigVariant = Game::Config::FConfigVariant;

    static FConfigRegistry& Get();

    template<typename T>
    void RegisterConfig(FName Name, T DefaultValue);

    template<typename T>
    ESetConfigResult SetConfig(FName Name, T NewValue);

    template<typename T>
    T GetConfig(FName Name, T FallbackValue = T{}) const;

    template<typename T>
    TOptional<T> GetConfigOptional(FName Name) const;

    template<typename T>
    TSharedRef<FObserverSubscription> WatchConfig(FName Name, TFunction<void(const T&)> OnChanged);

    [[nodiscard]] bool IsRegistered(FName Name) const;
    [[nodiscard]] uint32 GetVersion() const noexcept;
};
```

### 接口详细规范

#### `Get`
```cpp
static FConfigRegistry& Get();
```
- 获取注册中心全局单例（函数局部静态单例，C++11 保证线程安全初始化）。

#### `RegisterConfig<T>`
```cpp
template<typename T>
void RegisterConfig(FName Name, T DefaultValue);
```
- **职责**：幂等注册默认值。仅在键尚未注册时写入默认值；若已存在则保持当前值不变，不覆盖既有值。

#### `SetConfig<T>`
```cpp
template<typename T>
ESetConfigResult SetConfig(FName Name, T NewValue);
```
- **职责**：运行时覆盖写入配置新值（未注册时自动注册），并在出锁后广播通知。
- **只读键保护**：
  - 以 `Env.` 开头的配置永远为只读，运行时拦截写入并报错；
  - 在 Shipping 构建下，以 `Dev.` 开头以及 `Feature.GM` 键被自动设为只读。
- **返回值**：成功写入返回 `ESetConfigResult::Applied`；若键受保护或为只读则拒绝并返回 `ESetConfigResult::RejectedReadOnly`。

#### `GetConfig<T>`
```cpp
template<typename T>
T GetConfig(FName Name, T FallbackValue = T{}) const;
```
- **职责**：按指定强类型读取配置值。
- **并发保证**：在 `FRWLock` 读锁内完成纯值拷贝后立即出锁，返回纯值副本，调用方处于零锁状态。
- **异常处理**：若键不存在，返回 `FallbackValue`；若键存在但类型不匹配，在非 Shipping 模式下触发 `ensureMsgf` 强断言。

#### `GetConfigOptional<T>`
```cpp
template<typename T>
TOptional<T> GetConfigOptional(FName Name) const;
```
- **职责**：按值取值的可选语义版本。
- **返回值**：若键存在且类型匹配，返回包含该值的 `TOptional<T>`；若键未注册或类型不匹配，返回未设置的 `TOptional`。

#### `WatchConfig<T>`
```cpp
template<typename T>
TSharedRef<FObserverSubscription> WatchConfig(FName Name, TFunction<void(const T&)> OnChanged);
```
- **职责**：订阅该配置的数值变更（响应式热调，零轮询）。
- **返回值**：`TSharedRef<FObserverSubscription>` RAII 守卫对象。业务方必须持有该守卫；当守卫对象析构时自动取消订阅。

#### `IsRegistered`
```cpp
[[nodiscard]] bool IsRegistered(FName Name) const;
```
- **职责**：查询指定配置键是否已注册。

#### `GetVersion`
```cpp
[[nodiscard]] uint32 GetVersion() const noexcept;
```
- **职责**：获取全局版本号（每次注册或覆盖写入时自增）。

---

## 3. 蓝图函数库 `UConfigBPLibrary`

为蓝图提供便捷读写与判断接口：

| 蓝图节点 | 说明 |
| :--- | :--- |
| `GetConfigBool(Key, DefaultValue)` | 读取布尔配置 |
| `GetConfigInt(Key, DefaultValue)` | 读取 int32 配置 |
| `GetConfigInt64(Key, DefaultValue)` | 读取 int64 配置 |
| `GetConfigFloat(Key, DefaultValue)` | 读取 float 配置 |
| `GetConfigDouble(Key, DefaultValue)` | 读取 double 配置 |
| `GetConfigString(Key, DefaultValue)` | 读取 FString 配置 |
| `GetConfigName(Key, DefaultValue)` | 读取 FName 配置 |
| `GetConfigVector(Key, DefaultValue)` | 读取 FVector 配置 |
| `GetConfigRotator(Key, DefaultValue)` | 读取 FRotator 配置 |
| `GetConfigGameplayTag(Key, DefaultValue)` | 读取 FGameplayTag 配置（UHT 不支持标签类型默认参数，须显式传入默认值） |
| `IsConfigRegistered(Key)` | 检查配置键是否已注册 |
| `SetConfigBool(Key, Value)` | 写入布尔配置 |
| `SetConfigInt(Key, Value)` | 写入 int32 配置 |
| `SetConfigInt64(Key, Value)` | 写入 int64 配置 |
| `SetConfigFloat(Key, Value)` | 写入 float 配置 |
| `SetConfigDouble(Key, Value)` | 写入 double 配置 |
| `SetConfigString(Key, Value)` | 写入 FString 配置 |
| `SetConfigName(Key, Value)` | 写入 FName 配置 |
| `SetConfigVector(Key, Value)` | 写入 FVector 配置 |
| `SetConfigRotator(Key, Value)` | 写入 FRotator 配置 |
| `SetConfigGameplayTag(Key, Value)` | 写入 FGameplayTag 配置 |

未注册键一律返回调用方传入的默认值；写入统一经 `FConfigRegistry::SetConfig`，只读键拒绝语义由注册表保证。

