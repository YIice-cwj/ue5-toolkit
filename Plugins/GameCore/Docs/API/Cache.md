# GameCore 版本化缓存与淘汰策略 API 参考

本文档汇总 `GameCore` 提供的三策略版本化缓存与连续节点池淘汰原语（全部位于 `Cache/` 目录）：
- `FVersionedCache<T>`：Eager 全量快照、Lazy 惰性填充与 LRU 限长淘汰三策略聚合缓存容器
- `FCachePolicy`：配置中心驱动的动态策略解析与自适应阈值决策
- `FLruIndexPool`：基于连续数组与 Clock 二次机会（Second-Chance）原子热度位的零堆分配 LRU 双向链表索引池

---

## 1. 三策略聚合版本化缓存 `FVersionedCache<T>`

头文件：`#include "Cache/FVersionedCache.h"`

### 核心特性与并发安全模型
- **脱锁生命周期安全（Pointer Stability & Lifetime Guarantee）**：
  - 查询接口 `FindShared(Key)` 返回只读数据共享指针 `TSharedPtr<const ValueType>`；
  - 外部读取线程拿到共享指针后，即使缓存立即执行 `Rebuild()` 重构整表、被 LRU 驱逐或调用 `Invalidate(Key)` 精确剔除，由于外部强引用计数 $> 0$，该数据实例绝不会被提前析构；
  - 外部使用完毕后对象正常安全释放，彻底消灭野指针与生命周期竞态；
- **双重检查代次守卫（Double-checked Generation Guard）**：
  - 在 Lazy 与 LRU 策略中，未命中的单条构建函数（`LocalBuildFn(Key)`）**在完全脱离读写锁的环境下执行**，防止耗时的数据加载或反序列化长时间阻塞其他读取线程；
  - 构建完成后重新申请写锁，并校验代次 `Generation == ObservedGeneration`：若构建期间发生了并发 `Rebuild()` 或 `Reset()`，则丢弃陈旧数据并重新取数，严禁陈旧快照覆盖最新数据；
- **锁外内存回收（Out-of-lock Memory Reclamation）**：
  - 重建或重置时，内部仅在写锁内通过 `MoveTemp` 转移被淘汰的 `ValueMap` 与有效键集的所有权至 `FRetiredStorage`；
  - 释放锁后，`FRetiredStorage` 离开局部作用域自然析构，消灭因锁内批量析构大容器引发的微秒级停顿；
- **单键精确失效（`Invalidate`）**：
  - 支持单条记录精确剔除，完美契合热更新配置目录监听，无需全量整表重建。

```cpp
struct FItemConfig {
    FString ItemName;
    int32 Price = 0;
};

// 1. 初始化针对 "Item" 业务域的缓存实例
FVersionedCache<FItemConfig> ItemCache(TEXT("Item"));

// 2. 预热/重构缓存（传入所有已知键与按键构建闭包）
TArray<FName> AllItemKeys = { TEXT("Sword"), TEXT("Shield"), TEXT("Potion") };
ItemCache.Rebuild(AllItemKeys, [](FName Key) -> FItemConfig {
    FItemConfig Config;
    Config.ItemName = Key.ToString();
    Config.Price = 100;
    return Config;
});

// 3. 并发安全查询（返回只读共享指针）
TSharedPtr<const FItemConfig> Item = ItemCache.FindShared(TEXT("Sword"));
if (Item.IsValid()) {
    UE_LOG(LogTemp, Log, TEXT("Item: %s, Price: %d"), *Item->ItemName, Item->Price);
}

// 4. 精确失效单个键（如热重载单个文件）
ItemCache.Invalidate(TEXT("Shield"));
```

---

## 2. 策略配置与自适应决策 `FCachePolicy`

头文件：`#include "Cache/FCachePolicy.h"`

### 策略枚举与判定规则
```cpp
enum class ECacheStrategy : uint8 {
    Automatic, // 自动判定策略（默认）
    Eager,     // 全量预热构建（不可变快照替换）
    Lazy,      // 惰性按需填充（单条动态构建常驻）
    Lru        // LRU 限长淘汰（连续节点池）
};
```

- **配置键命名约定**：
  `FCachePolicy::Resolve(Domain)` 自动从 `FConfigRegistry` 读取 `Cache.{Domain}.*` 开关与参数：
  - `Cache.{Domain}.Strategy`：显式指定策略（`"automatic"` / `"eager"` / `"lazy"` / `"lru"`，大小写不敏感）；
  - `Cache.{Domain}.LazyMinRows`：数据行数 $\ge$ 该值时启用 Lazy；
  - `Cache.{Domain}.LruMinRows`：数据行数 $\ge$ 该值时启用 LRU（优先级高于 Lazy）；
  - `Cache.{Domain}.LruCapacity`：LRU 最大缓存容量，内部严格钳制在安全区间 $[1, 1000000]$。
- **自适应判定流向**：
  若策略配置为 `Automatic`：
  1. 若 `RowCount >= LruMinRows` $\rightarrow$ 生效策略为 `Lru`；
  2. 否则若 `RowCount >= LazyMinRows` $\rightarrow$ 生效策略为 `Lazy`；
  3. 未达任何阈值（小数据集） $\rightarrow$ 生效策略为 `Eager`。

```ini
; 示例：在 DefaultGame.ini 中微调配置域缓存策略
[GameCore.Config]
+StringConfigs=(Key="Cache.SkillTable.Strategy", Value="automatic")
+IntConfigs=(Key="Cache.SkillTable.LazyMinRows", Value="500")
+IntConfigs=(Key="Cache.SkillTable.LruMinRows", Value="5000")
+IntConfigs=(Key="Cache.SkillTable.LruCapacity", Value="1024")
```

---

## 3. Clock 二次机会双向链表索引池 `FLruIndexPool`

头文件：`#include "Cache/FLruIndexPool.h"`

### 架构设计与并发无锁热度更新
传统的 LRU 容器在每次读取命中时都需要获取互斥写锁调整链表指针，在高并发读取场景下会产生极其严重的锁颠簸。`FLruIndexPool` 采用工业级改进设计：

- **零每键堆内存分配**：
  - 内部预分配连续节点池 `TArray<FLruNode> Pool` 与 `KeyToNode` 哈希表；
  - 节点前驱 `Prev`、后继 `Next` 均为 `int32` 索引，未占用的槽位通过 `FreeHead` 构成空闲回收单向链表，内存连续且 CPU 缓存极其友好；
- **并发读零锁热度标记（Clock Second-Chance）**：
  - `FindShared` 在处于读锁环境下，调用 `LruPool.MarkReferenced(Key)`；
  - 仅执行原子操作 `FPlatformAtomics::InterlockedExchange(&Pool[Idx].HotBit, 1)` 将热度位置 1，**全程不争抢写锁**；
- **淘汰时平滑晋升（EvictTail）**：
  - 当容量超出上限触发淘汰时，在写锁内扫描尾部节点 `Tail`；
  - 若尾部节点的 `HotBit == 1`，则清零热度位并将其移回链表头部给予二次机会，继续向前扫描；
  - 仅当尾部节点处于绝对冷态（`HotBit == 0`）时，才将其真正淘汰出池并回收槽位。

```cpp
// 内部由 FVersionedCache 自动管理，开发者亦可单独使用于自定义紧凑 LRU 逻辑
FLruIndexPool Lru;
Lru.Reserve(1024);

const int32 Node = Lru.AllocateNode(TEXT("Player_1001"));
Lru.InsertAtHead(Node);

// 并发读命中时打标（零写锁争用）
Lru.MarkReferenced(TEXT("Player_1001"));

// 淘汰尾部最久未访问条目（自动处理二次机会晋升）
const FName EvictedKey = Lru.EvictTail();
```
