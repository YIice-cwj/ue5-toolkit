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

    static TSharedPtr<FConfigRegistry> GetShared(const UObject* WorldContextObject = nullptr);
    static FConfigRegistry& Get(const UObject* WorldContextObject = nullptr);

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
    [[nodiscard]] bool IsReadOnly(FName Name) const;
    [[nodiscard]] TOptional<FConfigVariant> GetRawConfigOptional(FName Name) const;
    [[nodiscard]] uint32 GetVersion() const noexcept;
    void InheritFrom(const FConfigRegistry& Other);
};
```

### 接口详细规范

#### `GetShared`
```cpp
static TSharedPtr<FConfigRegistry> GetShared(const UObject* WorldContextObject = nullptr);
```
- 获取配置注册中心的共享指针（推荐跨帧或持久持有使用，完全杜绝 World 销毁导致的裸引用悬垂崩溃）。

#### `Get`
```cpp
static FConfigRegistry& Get(const UObject* WorldContextObject = nullptr);
```
- 获取配置注册中心引用；支持基于 World 上下文的 PIE 多实例隔离（WITH_EDITOR 下自动按 UWorld 分离并继承全局默认项，脱离 World 或传 nullptr 时返回全局根单例）。
- **注意**：返回的裸引用生命周期与对应 UWorld 绑定，仅限当前调用栈或单帧内临时使用，严禁跨帧持久缓存；持久持有请使用 `GetShared()`。

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

#### `GetRawConfigOptional`
```cpp
[[nodiscard]] TOptional<FConfigVariant> GetRawConfigOptional(FName Name) const;
```
- **职责**：按类型擦除变体快照读取已注册配置值。
- **使用场景**：供外部配置源桥接器（`FConfigSourceBridge`）等基础设施在无需预先知道具体类型的情况下，反查已注册配置项的实际类型并进行自动推导。若键未注册则返回未设置的 `TOptional`。

---

## 3. 零哈希高性能响应式句柄 `TCachedConfigHandle<T>`

头文件：`#include "Config/TCachedConfigHandle.h"`

### 痛点与设计目标
在每秒执行成千上万次的高频热点链路（如物理更新、移动计算、战斗数值计算）中，传统的 `FConfigRegistry::GetConfig(FName)` 每次都需要进行 `FName` 哈希查找、遍历散列表以及争用全局 `FRWLock` 读写锁。  
`TCachedConfigHandle<T>` 专为高频读取设计：
- **$O(1)$ 局部缓存读取**：消灭哈希计算与字典查找开销，读取直接解指针/读取本地缓存，极致轻量；
- **响应式自同步**：内部持有弱引用订阅 `FObserverSubscription`。当外部通过 `RegisterConfig` 或 `SetConfig` 热重载配置时，句柄自动同步最新值，无需调用方写轮询或样板监听代码；
- **线程安全与自然语法**：支持按值读取 `Get()`、隐式类型转换 `operator T()` 与解引用操作符 `*Handle`；
- **PIE 多实例感知**：支持指定 `WorldContextObject` 绑定对应 PIE 独立配置，并提供 `Rebind()` 支持切关动态重定向。

### 接口与使用示例
```cpp
// 1. 在高频系统类或结构体中定义句柄成员（提供键名与默认回退值）
class UCombatComponent : public UActorComponent {
private:
    TCachedConfigHandle<float> DamageScaleHandle{FName(TEXT("Combat.DamageScale")), 1.0f};
    TCachedConfigHandle<FVector> KnockbackDirHandle{FName(TEXT("Combat.KnockbackDir")), FVector::UpVector};

public:
    void TickComponent(...) override {
        // 零哈希极速读取，支持隐式类型转换
        const float DamageScale = DamageScaleHandle; 
        const FVector Knockback = *KnockbackDirHandle;
    }
};

// 2. 注册热更新监听（可选）
DamageScaleHandle.SetOnChanged([](float NewScale) {
    UE_LOG(LogTemp, Log, TEXT("伤害系数热更生效: %f"), NewScale);
});

// 3. 状态查询与生命周期
if (DamageScaleHandle.IsBound()) {
    // 已从配置中心成功绑定已注册值
}
```

---

## 4. 蓝图函数库 `UConfigBPLibrary`

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

---

## 5. 外部配置源桥接器 `FConfigSourceBridge`

头文件：`#include "Config/FConfigSourceBridge.h"`

用于将 `.ini` 配置节（如 `GGameIni`）与启动命令行参数（如 `-Config.Combat.Crit=0.5`）安全注入 `FConfigRegistry`：
- `LoadIniSection(ConfigFilename, SectionName)`：读取指定 INI 节中的全部 `Key=Value` 并按类型解析写入。
- `ApplyCommandLineOverrides(CommandLine, ArgPrefix = TEXT("Config."))`：支持双引号包裹空格字符串（如 `-Config.Motd="Hello World"`）与前导 `-`/`--` 前缀。
- `ApplyKeyValueMap(KeyValues)` / `ApplySingleEntry(RawKeySpec, RawValue)`：
  - 若键已预先在 `FConfigRegistry` 中注册，自动按已注册的变体类型精确反序列化，无需手写类型标签；
  - 若键尚未注册，支持显式类型前缀（`bool:`、`int32:`、`int64:`、`float:`、`double:`、`string:`、`name:`、`vector:`、`rotator:`、`tag:`），未加前缀默认按 `FString` 落库；
  - 在外部不可信边界前置调用 `FConfigRegistry::IsReadOnly` 拦截对 `Env.*` 等只读键的覆盖尝试，安全忽略且不触发断言。

---

## 6. 调试指令迁移指引 (FGameCheatRegistry)

> [!NOTE]
> **组件解耦与迁移提示**：
> 模块化 GM / 作弊调试指令注册中心（`FGameCheatRegistry`）已在多插件解耦架构中剥离至专职调试插件 **`Plugins/GameDebug`**（头文件 `#include "GameDebugCheatRegistry.h"`）。
> `GameCore` 专注于纯底层配置生命周期管理；若需在开发期注册控制台 GM 命令，请直接依赖并引入 `GameDebug` 模块。



