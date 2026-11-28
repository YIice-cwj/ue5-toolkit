# GameCore 配置注册中心 API

> **一句话**：线程安全、类型安全、支持响应式订阅与外部配置源注入的动态配置注册表。
> **头文件**：`Config/FConfigRegistry.h`、`Config/TCachedConfigHandle.h`、`Config/FConfigSourceBridge.h`；蓝图库 `Config/UConfigBPLibrary.h` · **线程**：读写接口内部 `FRWLock` 读写分离，读接口锁内拷贝后出锁返回纯值；订阅回调在出锁后派发 · **失败语义**：写入返回 `ESetConfigResult`；未注册或类型不匹配按 `Fallback` 回退，类型不匹配同时触发 `ensureAlwaysMsgf`

## 30 秒速览

- **存储**：`FConfigVariant` 为 24 字节类型擦除变体，支持 `bool` / `int32` / `int64` / `float` / `double` / `FString` / `FName` / `FVector` / `FRotator` / `FGameplayTag`。
- **读写**：`RegisterConfig` 仅首次写入默认值；`SetConfig` 覆盖或自动注册并自增版本号；`GetConfig` / `GetConfigOptional` 按强类型取值。
- **只读保护**：`Env.*` 恒只读；`Dev.*` 与 `Feature.GM` 在对应编译开关关闭时（默认 Shipping）只读，`SetConfig` 返回 `RejectedReadOnly`。
- **响应式**：`WatchConfig` 返回 `FObserverSubscription` RAII 守卫，守卫析构即退订；不主动回放当前值。
- **高频读取**：`TCachedConfigHandle<T>` 缓存配置值，标量走原子读，避免每次 `FName` 哈希与全局锁竞争。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `FConfigRegistry::GetShared` | 获取强引用注册表 | 线程安全；跨帧持久持有必须用此入口 |
| `FConfigRegistry::Get` | 获取裸引用注册表 | 随对应 `UWorld` 销毁失效，仅限当前调用栈或单帧内使用 |
| `FConfigRegistry::RegisterConfig<T>` | 注册配置默认值 | 幂等，已注册键保持原值 |
| `FConfigRegistry::SetConfig<T>` | 覆盖或自动注册配置 | 只读键返回 `RejectedReadOnly` 且不改值；成功写入自增版本 |
| `FConfigRegistry::GetConfig<T>` | 强类型读取配置 | 未注册或类型不匹配回退 `FallbackValue`；类型不匹配触发 `ensureAlwaysMsgf` |
| `FConfigRegistry::GetConfigOptional<T>` | 强类型可选读取 | 未命中返回未设置的 `TOptional` |
| `FConfigRegistry::WatchConfig<T>` | 订阅配置变更 | 回调在写入线程出锁后执行；不回放当前值；须持有返回守卫 |
| `FConfigRegistry::IsReadOnly` | 判定键是否只读 | `Env.*` 恒只读；未注册时按内置规则判断 |
| `TCachedConfigHandle<T>::Get` | 高频读取缓存配置值 | 标量原子无锁读，复合类型持读锁按值拷贝 |
| `FConfigSourceBridge::LoadIniSection` | 注入 INI 指定节键值 | 只读键安全忽略；返回成功写入数量 |

## 核心类型

### `FConfigVariant`

头文件：`#include "Config/FConfigVariant.h"`

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

- 最大成员 `24` 字节，强类型严格匹配，不做隐式数值截断。
- `Game::Config::FindVariantValue<T>(Variant)` 只读提取类型化指针，不匹配返回 `nullptr`。

### `ESetConfigResult`

```cpp
enum class ESetConfigResult : uint8 {
    Applied,
    RejectedReadOnly,
};
```

| 枚举值 | 说明 |
| :--- | :--- |
| `Applied` | 写入成功，包含首次注册与运行时覆盖 |
| `RejectedReadOnly` | 目标键为只读，写入被拒绝 |

## 配置注册表 `FConfigRegistry`

头文件：`#include "Config/FConfigRegistry.h"`

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

    [[nodiscard]] int32 Num() const;
    [[nodiscard]] bool IsEmpty() const;
    int32 GetRegisteredKeys(TArray<FName>& OutKeys) const;
    [[nodiscard]] bool IsRegistered(FName Name) const;
    [[nodiscard]] bool IsReadOnly(FName Name) const;
    [[nodiscard]] TOptional<FConfigVariant> GetRawConfigOptional(FName Name) const;
    [[nodiscard]] uint32 GetVersion() const noexcept;
    void InheritFrom(const FConfigRegistry& Other);
};
```

### 实例获取

| 接口 | 说明 |
| :--- | :--- |
| `GetShared(WorldContextObject)` | 返回强引用共享指针；传空或无效 World 时返回进程级全局根注册表 |
| `Get(WorldContextObject)` | 返回裸引用；有效 World 上下文返回按 `UWorld` 隔离并继承全局默认项的实例 |

- **生命周期**：`Get()` 返回的引用随对应 `UWorld` 销毁失效，仅限当前调用栈或单帧内使用；跨帧持久持有必须用 `GetShared()`。
- **线程**：两个入口均线程安全；World 隔离表由独立的 `FRWLock` 保护。

### 注册与写入

```cpp
template<typename T>
void RegisterConfig(FName Name, T DefaultValue);

template<typename T>
ESetConfigResult SetConfig(FName Name, T NewValue);
```

| 参数 | 说明 |
| :--- | :--- |
| `Name` | 配置键 |
| `DefaultValue` | `RegisterConfig` 的默认值；仅键未注册时写入 |
| `NewValue` | `SetConfig` 的新值；键未注册时自动注册 |

- **语义**：`RegisterConfig` 幂等，已注册键保持原值；`SetConfig` 每次成功写入都会自增条目版本与全局版本。
- **只读规则**：`Env.*` 恒只读；`GAME_ENABLE_DEBUG_DRAW == 0`（默认 Shipping）时 `Dev.*` 只读；`GAME_ENABLE_GM == 0`（默认 Shipping）时 `Feature.GM` 只读。
- **派发时序**：写入在写锁内完成，出锁后才派发订阅回调，持锁期间不调用用户代码。
- **失败语义**：只读键返回 `RejectedReadOnly` 且不修改值。

### 读取

```cpp
template<typename T>
T GetConfig(FName Name, T FallbackValue = T{}) const;

template<typename T>
TOptional<T> GetConfigOptional(FName Name) const;

[[nodiscard]] TOptional<FConfigVariant> GetRawConfigOptional(FName Name) const;
```

| 接口 | 说明 |
| :--- | :--- |
| `GetConfig<T>` | 键存在且类型匹配时返回当前值，否则返回 `FallbackValue` |
| `GetConfigOptional<T>` | 键存在且类型匹配时返回值，否则返回未设置的 `TOptional` |
| `GetRawConfigOptional` | 返回类型擦除变体快照，供 `FConfigSourceBridge` 等无需预知类型的设施反查 |

- **线程**：均在读锁内完成值拷贝后立即出锁，调用方处于零锁状态。
- **失败语义**：键未注册不报错；键存在但类型不匹配触发 `ensureAlwaysMsgf`（Shipping 下同样生效）并回退 `Fallback` / 未设置可选值。

### 订阅

```cpp
template<typename T>
TSharedRef<FObserverSubscription> WatchConfig(FName Name, TFunction<void(const T&)> OnChanged);
```

- 业务方必须持有返回的 `FObserverSubscription`；守卫析构即自动退订。
- 仅在后续 `RegisterConfig` / `SetConfig` 写入时回调，**不会主动回放当前值**。
- 空回调返回空守卫；订阅时若已注册值类型不匹配，触发 `ensureAlwaysMsgf` 并返回空守卫。
- 回调在写入线程、出锁后执行。

### 查询与维护

| 接口 | 说明 |
| :--- | :--- |
| `Num()` / `IsEmpty()` | 已注册条目数 / 是否为空 |
| `GetRegisteredKeys(OutKeys)` | 收集全部键名，返回条目数 |
| `IsRegistered(Name)` | 键是否已注册 |
| `IsReadOnly(Name)` | 当前构建下是否为只读键；未注册时按内置规则判断 |
| `GetVersion()` | 全局版本号，每次注册或覆盖写入自增 |
| `InheritFrom(Other)` | 复制另一注册表的全部条目与版本号，用于 PIE 实例继承全局默认配置 |

模块卸载时由 `FConfigRegistry::Shutdown()` 释放 World 隔离实例；自动化测试可用 `ClearWorldRegistriesForTesting()` 清理缓存。

## 高频读取 `TCachedConfigHandle<T>`

头文件：`#include "Config/TCachedConfigHandle.h"`

```cpp
template<typename T>
class TCachedConfigHandle {
public:
    TCachedConfigHandle() = default;
    explicit TCachedConfigHandle(
        FName InKeyName,
        T InFallbackValue = T{},
        const UObject* WorldContextObject = nullptr
    );

    void Rebind(const UObject* WorldContextObject = nullptr);
    void Reset(FName InKeyName, T InFallbackValue = T{}, const UObject* WorldContextObject = nullptr);
    void Reset();

    [[nodiscard]] T Get() const;
    operator T() const;
    T operator*() const;

    struct FArrowProxy {
        T Value;
        const T* operator->() const noexcept;
    };
    FArrowProxy operator->() const;

    [[nodiscard]] FName GetKey() const;
    [[nodiscard]] T GetFallback() const;
    [[nodiscard]] bool IsBound() const;
    [[nodiscard]] bool IsValid() const;
    void SetOnChanged(TFunction<void(const T&)> InCallback);
};
```

| 接口 | 说明 |
| :--- | :--- |
| `Get()` | 返回当前缓存值或回退值；标量走 `std::atomic<T>` 无锁读，复合类型持读锁按值拷贝 |
| `operator T()` / `operator*()` / `operator->()` | 自然语法读取；`operator->` 返回局部只读代理，避免锁逃逸 |
| `IsBound()` | 是否已从注册表取得有效配置（非回退态） |
| `IsValid()` | 是否已绑定非空键名 |
| `SetOnChanged` | 设置热更回调；可在绑定前设置，重绑定后保留 |
| `Rebind(WorldContextObject)` | 以当前键重新绑定到目标 World 的注册表 |
| `Reset(Key, Fallback, WorldContext)` | 更换键并重新绑定；无参 `Reset()` 清空全部状态 |

- **线程**：`Get()` 对算术类型无锁原子读，对 `FString` / `FName` / `FVector` / `FRotator` / `FGameplayTag` 走 `FRWLock` 读锁按值返回。
- **生命周期**：句柄可拷贝，多个副本共享同一订阅状态；句柄析构时订阅随共享状态释放。
- **类型约束**：`T` 仅支持 `FConfigVariant` 中的十种类型，其余类型触发 `static_assert`。
- **性能参考**：标量 `Get()` 实测约 `2.3 ns/op`，相比 `FConfigRegistry::GetConfig` 提速约 `2.80×`（历史 Development 构建参考值，详见 [Performance.md](../Architecture/Performance.md)）。

```cpp
class UCombatComponent : public UActorComponent {
private:
    TCachedConfigHandle<float> DamageScaleHandle{FName(TEXT("Combat.DamageScale")), 1.0f};
    TCachedConfigHandle<FVector> KnockbackDirHandle{FName(TEXT("Combat.KnockbackDir")), FVector::UpVector};

public:
    void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override {
        const float DamageScale = DamageScaleHandle;
        const FVector Knockback = *KnockbackDirHandle;
    }
};

DamageScaleHandle.SetOnChanged([](float NewScale) {
    UE_LOG(LogTemp, Log, TEXT("伤害系数热更生效: %f"), NewScale);
});

if (DamageScaleHandle.IsBound()) {
}
```

## 外部配置源桥接 `FConfigSourceBridge`

头文件：`#include "Config/FConfigSourceBridge.h"`

```cpp
class GAMECORE_API FConfigSourceBridge {
public:
    FConfigSourceBridge() = delete;

    static int32 LoadIniSection(
        const FString& ConfigFilename,
        const FString& SectionName,
        const UObject* WorldContextObject = nullptr
    );
    static int32 ApplyKeyValueMap(
        const TMap<FString, FString>& KeyValues,
        const UObject* WorldContextObject = nullptr
    );
    static int32 ApplyCommandLineOverrides(
        const TCHAR* CommandLine,
        const FString& ArgPrefix = TEXT("Config."),
        const UObject* WorldContextObject = nullptr
    );
    static bool ApplySingleEntry(
        const FString& RawKeySpec,
        const FString& RawValue,
        const UObject* WorldContextObject = nullptr
    );
};
```

| 接口 | 说明 |
| :--- | :--- |
| `LoadIniSection(ConfigFilename, SectionName, ...)` | 读取 INI 指定节的全部 `Key=Value` 并注入，返回成功写入数量 |
| `ApplyKeyValueMap(KeyValues, ...)` | 批量注入内存键值对，返回成功写入数量 |
| `ApplyCommandLineOverrides(CommandLine, ArgPrefix, ...)` | 解析 `-Config.Key=Value` / `--Config.Key=Value`，支持双引号包裹空格值 |
| `ApplySingleEntry(RawKeySpec, RawValue, ...)` | 写入单条；成功返回 `true`，只读拦截或格式非法返回 `false` |

- **类型推导**：键已注册时按已注册变体类型精确反序列化；未注册时使用显式前缀 `bool:` / `int32:` / `int64:` / `float:` / `double:` / `string:` / `name:` / `vector:` / `rotator:` / `tag:`，未加前缀按 `FString` 落库。
- **前缀别名**：`int` / `FString` / `FName` / `FVector` / `FRotator` / `FGameplayTag` 大小写不敏感。
- **只读边界**：写入前调用 `FConfigRegistry::IsReadOnly` 拦截只读键，安全忽略且不触发断言。
- **失败语义**：非法格式或只读键不计入返回的写入数量。

## 蓝图桥接 `UConfigBPLibrary`

头文件：`#include "Config/UConfigBPLibrary.h"`

未注册键一律返回蓝图传入的默认值；写入统一经 `FConfigRegistry::SetConfig`，只读拒绝语义由注册表保证。

<details>
<summary>展开蓝图节点清单</summary>

| 蓝图节点 | 说明 |
| :--- | :--- |
| `GetConfigBool` / `GetConfigInt` / `GetConfigInt64` | 读取布尔 / int32 / int64 |
| `GetConfigFloat` / `GetConfigDouble` | 读取 float / double |
| `GetConfigString` / `GetConfigName` | 读取 `FString` / `FName` |
| `GetConfigVector` / `GetConfigRotator` | 读取 `FVector` / `FRotator` |
| `GetConfigGameplayTag` | 读取 `FGameplayTag`；UHT 不支持标签类型默认参数，须显式传入默认值 |
| `IsConfigRegistered` | 检查配置键是否已注册 |
| `GetConfigVersion` | 获取全局版本号 |
| `GetRegisteredConfigCount` | 获取已注册配置项总数 |
| `SetConfigBool` / `SetConfigInt` / `SetConfigInt64` | 写入布尔 / int32 / int64 |
| `SetConfigFloat` / `SetConfigDouble` | 写入 float / double |
| `SetConfigString` / `SetConfigName` | 写入 `FString` / `FName` |
| `SetConfigVector` / `SetConfigRotator` | 写入 `FVector` / `FRotator` |
| `SetConfigGameplayTag` | 写入 `FGameplayTag` |

</details>

## 常见错误与边界

- **跨帧缓存 `Get()` 引用**：`FConfigRegistry::Get()` 的裸引用随 World 销毁失效，跨帧持有请改用 `GetShared()`。
- **只读键写入**：`Env.*` 永远拒绝；默认 Shipping 下 `Dev.*` 与 `Feature.GM` 也返回 `RejectedReadOnly`，调试期需打开对应编译开关。
- **类型不匹配**：读取会触发 `ensureAlwaysMsgf` 并回退默认值；写入前应保证 `SetConfig<T>` 的 `T` 与注册类型一致。
- **订阅守卫被提前释放**：`WatchConfig` 返回的 `TSharedRef` 未保存时订阅立即失效；必须由业务对象持有。
- **误以为订阅会回放**：`WatchConfig` 不推送当前值，初始值需另用 `GetConfig` 读取；`TCachedConfigHandle` 已在构造时完成初始拉取。
- **高频链路直接 `GetConfig`**：每帧热点应使用 `TCachedConfigHandle<T>`，避免重复哈希查找与全局读写锁竞争。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [配置与开关指南](../Guides/ConfigAndSwitchesGuide.md)
- [快速上手](../Guides/QuickStart.md)
