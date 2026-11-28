# GameCore 缓存 API

> **一句话**：三策略版本化缓存、配置驱动的缓存策略解析与 Clock 二次机会 LRU 索引池。
> **头文件**：`Cache/FVersionedCache.h`、`Cache/FCachePolicy.h`、`Cache/FLruIndexPool.h` · **线程**：`FVersionedCache` 的公开接口可并发调用（内部 `FRWLock` 读写分离）；`FLruIndexPool` 非线程安全，须由调用方持锁 · **失败语义**：未命中 / 未缓存键返回空共享指针或 `false`；未知策略字符串回退 `Automatic` 并记录 Warning

## 30 秒速览

- **容器**：`FVersionedCache<T>` 按 `Cache.{Domain}.*` 配置在 Eager / Lazy / LRU 三种策略间自动选择。
- **指针稳定**：`FindShared` 返回 `TSharedPtr<const ValueType>`，缓存重建、驱逐或单键失效都不会提前析构外部正在使用的数据。
- **代次守卫**：Lazy / LRU 的锁外构建完成后会校验 `Generation`，期间发生 `Rebuild()` / `Reset()` 则丢弃陈旧构建结果。
- **策略**：`FCachePolicy::Resolve(Domain)` 读取全局 `FConfigRegistry`；行数先比 `LruMinRows`、再比 `LazyMinRows`，均未达到则 Eager。
- **索引池**：`FLruIndexPool` 用连续数组 + `int32` 前后驱 + 原子 `HotBit` 实现零每键堆分配的 LRU。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `FVersionedCache<T>::Rebuild` | 按全量键集重建主表 | 可跨线程；空构建闭包等价 `Reset()` |
| `FVersionedCache<T>::FindShared` | 并发安全查找单键 | 可跨线程；未命中返回空指针；外部共享指针延长实例生命周期 |
| `FVersionedCache<T>::Invalidate` | 精确失效单键 | 可跨线程；键原本未缓存返回 `false` |
| `FVersionedCache<T>::Reset` | 清空全部状态并自增代次 | 可跨线程；对象不可拷贝、不可移动 |
| `FCachePolicy::Resolve` | 按业务域解析缓存策略 | 经全局 `FConfigRegistry` 读取；未知策略回退 `Automatic` 并记 Warning |
| `FLruIndexPool::MarkReferenced` | 读取时原子置热位 | 唯一可在读锁下并发调用的接口 |
| `FLruIndexPool::Touch` | 置热位并移到链表头 | 需调用方持写锁 |
| `FLruIndexPool::EvictTail` | 淘汰尾部冷节点 | 需持写锁；链表为空返回 `NAME_None` |
| `FLruIndexPool::AllocateNode` | 分配或复用节点槽位 | 需持写锁；不会失败；槽位索引不可跨调用缓存 |

## 三策略缓存 `FVersionedCache<T>`

头文件：`#include "Cache/FVersionedCache.h"`

```cpp
template<typename T>
class FVersionedCache {
public:
    using ValueType = T;
    using ValueTypePtr = TSharedPtr<const ValueType>;
    using ValueMap = TMap<FName, ValueTypePtr>;

    explicit FVersionedCache(const FName& Domain);

    void Rebuild(const TArray<FName>& AllKeys, TFunction<ValueType(FName Key)> InBuildFn);
    [[nodiscard]] ValueTypePtr FindShared(FName Key) const;
    void Reset();
    bool Invalidate(FName Key);
    [[nodiscard]] int32 Num() const;
    [[nodiscard]] bool IsEmpty() const;
    [[nodiscard]] bool Contains(FName Key) const;
    [[nodiscard]] ECacheStrategy GetEffectiveStrategy() const;
    [[nodiscard]] const FName& GetDomain() const noexcept;
    [[nodiscard]] uint64 GetGeneration() const noexcept;
};
```

| 接口 | 说明 |
| :--- | :--- |
| `FVersionedCache(Domain)` | 构造时即调用 `FCachePolicy::Resolve(Domain)` 解析域策略 |
| `Rebuild(AllKeys, InBuildFn)` | 重建主表；Eager 在锁外构建全量快照，Lazy / LRU 只登记键集 |
| `FindShared(Key)` | 并发安全查找；Lazy / LRU 未命中时锁外单条构建并按代次校验写回 |
| `Invalidate(Key)` | 精确失效单键；键原本未缓存返回 `false` |
| `Reset()` | 清空全部状态并使代次自增 |
| `Contains(Key)` / `Num()` / `IsEmpty()` | 只读统计；`Contains` 不触发惰性构建 |
| `GetEffectiveStrategy()` / `GetGeneration()` / `GetDomain()` | 观测当前策略、代次与业务域 |

- **线程**：`Rebuild` / `FindShared` / `Reset` / `Invalidate` / 统计接口均可跨线程调用；对象本身不可拷贝、不可移动。
- **生命周期**：`FindShared` 返回的共享指针强引用数据实例；即使随后发生 `Rebuild()`、LRU 驱逐或 `Invalidate(Key)`，该实例在外部引用释放前不会被析构。
- **失败语义**：`Key.IsNone()` 或未命中返回空 `ValueTypePtr`；Eager 策略下未命中不会触发构建；`Rebuild` 传入空构建闭包等价于 `Reset()`。

### 策略判定 `ECacheStrategy`

```cpp
enum class ECacheStrategy : uint8 {
    Automatic,
    Eager,
    Lazy,
    Lru,
};
```

| 策略 | 行为 |
| :--- | :--- |
| `Automatic` | 由 `Rebuild` 时的总行数与阈值动态决定，默认值 |
| `Eager` | `Rebuild` 一次性构建全部条目并持有只读快照 |
| `Lazy` | 首次 `FindShared` 命中未缓存键时单条构建并常驻 |
| `Lru` | `Lazy` 基础上限制容量，超出后按 Clock 二次机会淘汰 |

- `Rebuild` 时重新解析一次策略，因此运行期修改配置后再次 `Rebuild` 即可切换策略。
- Lazy / LRU 只对 `Rebuild` 时传入的键集合做惰性构建；键不在集合内时 `FindShared` 直接返回空指针。

### 最小示例

```cpp
struct FItemConfig {
    FString ItemName;
    int32 Price = 0;
};

FVersionedCache<FItemConfig> ItemCache(TEXT("Item"));

TArray<FName> AllItemKeys = { TEXT("Sword"), TEXT("Shield"), TEXT("Potion") };
ItemCache.Rebuild(AllItemKeys, [](FName Key) -> FItemConfig {
    FItemConfig Config;
    Config.ItemName = Key.ToString();
    Config.Price = 100;
    return Config;
});

TSharedPtr<const FItemConfig> Item = ItemCache.FindShared(TEXT("Sword"));
if (Item.IsValid()) {
    UE_LOG(LogTemp, Log, TEXT("Item: %s, Price: %d"), *Item->ItemName, Item->Price);
}

ItemCache.Invalidate(TEXT("Shield"));
```

## 配置驱动策略 `FCachePolicy`

头文件：`#include "Cache/FCachePolicy.h"`

```cpp
struct GAMECORE_API FCachePolicy {
    static constexpr int64 MinLruCapacity = 1;
    static constexpr int64 MaxLruCapacity = 1000000;
    static constexpr int64 DefaultLruCapacity = 10000;

    static FCachePolicy Resolve(const FName& Domain);
    [[nodiscard]] int32 GetClampedLruCapacity() const noexcept;

    ECacheStrategy Strategy = ECacheStrategy::Automatic;
    int64 LazyMinRows = MAX_int64;
    int64 LruMinRows = MAX_int64;
    int64 LruCapacity = DefaultLruCapacity;
};
```

| 配置键 | 类型 | 默认值 | 说明 |
| :--- | :--- | :---: | :--- |
| `Cache.{Domain}.Strategy` | `FString` | `"auto"` | `auto` / `automatic` / `eager` / `lazy` / `lru`，大小写不敏感 |
| `Cache.{Domain}.LazyMinRows` | `int64` | `MAX_int64` | 行数 `>=` 该值时启用 Lazy，负值按 `0` 处理 |
| `Cache.{Domain}.LruMinRows` | `int64` | `MAX_int64` | 行数 `>=` 该值时启用 LRU，优先于 Lazy，负值按 `0` 处理 |
| `Cache.{Domain}.LruCapacity` | `int64` | `10000` | LRU 容量，读取时钳制到 `[1, 1000000]` |

- **线程**：`Resolve` 通过全局根注册表 `FConfigRegistry::Get()` 读取配置，遵循注册表的读写分离保证。
- **失败语义**：未知策略字符串记录 Warning 并回退 `Automatic`；配置键未注册时使用上表默认值。
- **自动判定顺序**：`RowCount >= LruMinRows` → `Lru`；否则 `RowCount >= LazyMinRows` → `Lazy`；否则 `Eager`。
- `Domain` 为空时 `Resolve` 触发 `ensureMsgf`。

```ini
[GameCore.Config]
Cache.SkillTable.Strategy=automatic
Cache.SkillTable.LazyMinRows=500
Cache.SkillTable.LruMinRows=5000
Cache.SkillTable.LruCapacity=1024
```

上述节可通过 `FConfigSourceBridge::LoadIniSection` 按 `Key=Value` 注入，详见 [ConfigRegistry](ConfigRegistry.md)。

## LRU 索引池 `FLruIndexPool`

头文件：`#include "Cache/FLruIndexPool.h"`

```cpp
struct FLruIndexPool {
    struct FLruNode {
        FName Key;
        int32 Prev = INDEX_NONE;
        int32 Next = INDEX_NONE;
        mutable volatile int32 HotBit = 0;
    };

    void Reset();
    void Reserve(int32 Capacity);
    [[nodiscard]] bool Contains(FName Key) const;
    [[nodiscard]] bool IsHead(FName Key) const;
    [[nodiscard]] FName GetHeadKey() const noexcept;
    [[nodiscard]] FName GetTailKey() const noexcept;
    [[nodiscard]] int32 GetCapacity() const noexcept;
    [[nodiscard]] int32 Num() const noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
    bool MarkReferenced(FName Key) const;
    void Touch(FName Key);
    bool Remove(FName Key);
    void RemoveFromChain(int32 NodeIdx);
    void InsertAtHead(int32 NodeIdx);
    int32 AllocateNode(FName Key);
    FName EvictTail();
};
```

| 接口 | 说明 |
| :--- | :--- |
| `Reserve(Capacity)` | 预分配节点池与 `KeyToNode` 容量 |
| `AllocateNode(Key)` | 复用空闲槽或扩容，返回节点池索引；已存在的键会重置并复用原槽位 |
| `InsertAtHead(NodeIdx)` / `RemoveFromChain(NodeIdx)` | 手工维护双向链表 |
| `MarkReferenced(Key)` | 读锁内仅做原子读 + 置热位，不争用写锁 |
| `Touch(Key)` | 置热位并移到链表头，需调用方持写锁 |
| `EvictTail()` | 淘汰尾部冷节点；热节点清零热位后晋升至头部并继续扫描 |
| `Remove(Key)` | 摘链、删除映射并将槽位回收到空闲链表 |

- **线程**：结构体本身无锁，除 `MarkReferenced` 可在读锁内并发调用外，其余接口要求调用方持有写锁。
- **生命周期**：节点以 `int32` 索引互链；被 `Reset()` / `Remove()` / `EvictTail()` 回收的槽位索引立即失效，不要跨调用缓存节点索引。
- **失败语义**：`Contains` / `MarkReferenced` / `Remove` / `IsHead` 对不存在的键返回 `false`；`AllocateNode` 不会失败；链表为空时 `EvictTail()` / `GetTailKey()` 返回 `NAME_None`。

```cpp
FLruIndexPool Lru;
Lru.Reserve(1024);

const int32 Node = Lru.AllocateNode(TEXT("Player_1001"));
Lru.InsertAtHead(Node);

Lru.MarkReferenced(TEXT("Player_1001"));

const FName EvictedKey = Lru.EvictTail();
```

## 常见错误与边界

- **把 `FindShared` 的返回值当缓存所有权**：返回的是共享快照；缓存侧移除不影响外部使用，但外部不应写回修改。
- **忘记传全量键集**：Lazy / LRU 只对 `Rebuild` 传入的 `AllKeys` 做惰性构建，键集缺失会让合法键永远查不到。
- **Eager 下期望惰性构建**：Eager 未命中直接返回空指针，不会调用构建闭包。
- **LRU 容量假设**：`LruCapacity` 读取时钳制到 `[1, 1000000]`，超出区间不会生效原值。
- **并发误用 `FLruIndexPool`**：仅 `MarkReferenced` 可在读锁下调用；其余写操作必须持写锁。
- **缓存策略热更**：`FVersionedCache` 构造时解析策略，修改配置后需再次 `Rebuild` 才会重新解析。

## 相关文档

- [插件 README](../../README.md)
- [缓存与内存架构](../Architecture/CachingAndMemory.md)
- [性能基准](../Architecture/Performance.md)
- [配置注册中心 API](ConfigRegistry.md)
