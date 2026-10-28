# GameCore 配置注册中心 API 参考

`FConfigRegistry` 提供了全生命周期、线程安全、类型安全的动态配置中心。上层业务、调试开关、网络环境等配置均可通过其集中注册与管理。

头文件：`#include "Config/FConfigRegistry.h"`  
蓝图库：`#include "Config/UConfigBPLibrary.h"`

---

## 1. 核心数据类型

### 1.1 `FConfigVariant`
配置存储的基础类型擦除变体，支持标量与引擎常用数学类型：
```cpp
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
```
成员上限 24 字节，强类型严格匹配，杜绝隐式数值截断。

---

## 2. `FConfigRegistry` 类方法

```cpp
namespace Game::Config {
class GAMECORE_API FConfigRegistry {
public:
    static FConfigRegistry& Get();

    template<typename T>
    void RegisterDefault(const FName& Key, T Value);

    template<typename T>
    bool SetConfig(const FName& Key, T Value);

    template<typename T>
    [[nodiscard]] T GetConfig(const FName& Key, const T& Fallback = T{}) const;

    template<typename T>
    [[nodiscard]] FObserverSubscription WatchConfig(const FName& Key, TFunction<void(const T&)> Callback);

    [[nodiscard]] bool HasKey(const FName& Key) const;
    void ResetAllConfigs();
};
}
```

### 接口详细规范

#### `Get`
```cpp
static FConfigRegistry& Get();
```
- 获取注册中心全局单例（函数局部静态单例，C++11 保证线程安全初始化）。

#### `RegisterDefault<T>`
```cpp
template<typename T>
void RegisterDefault(const FName& Key, T Value);
```
- **职责**：幂等注册默认值。若 Key 尚未存在，则写入该值；若已存在则保持当前值不变。

#### `SetConfig<T>`
```cpp
template<typename T>
bool SetConfig(const FName& Key, T Value);
```
- **职责**：写入配置新值，并在出锁后派发通知。
- **只读键保护**：
  - 以 `Env.` 开头的配置永远为只读，运行时拦截写入并报错；
  - 在 Shipping 构建下，以 `Dev.` 开头以及 `Feature.GM` 键被自动设为只读。
- **返回值**：成功写入返回 `true`；若键受保护或为只读则返回 `false`。

#### `GetConfig<T>`
```cpp
template<typename T>
[[nodiscard]] T GetConfig(const FName& Key, const T& Fallback = T{}) const;
```
- **职责**：按指定强类型读取配置值。
- **并发保证**：在 `FRWLock` 读锁内完成纯值拷贝后立即出锁，返回纯值副本，调用方处于零锁状态。
- **异常处理**：若键不存在，返回 `Fallback`；若键存在但类型不匹配，在非 Shipping 模式下触发 `ensureMsgf` 强断言。

#### `WatchConfig<T>`
```cpp
template<typename T>
[[nodiscard]] FObserverSubscription WatchConfig(const FName& Key, TFunction<void(const T&)> Callback);
```
- **职责**：订阅该配置的数值变更。
- **返回值**：`FObserverSubscription` RAII 守卫对象。当守卫对象析构时自动取消订阅。

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

