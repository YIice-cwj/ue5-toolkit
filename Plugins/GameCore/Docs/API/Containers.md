# GameCore 容器 API 参考

> **一句话**：代际槽位表、无锁 MPMC 环形缓冲区、连续内存排序字典与 64 位状态标签的公共 API。
> **头文件**：`Containers/TSlotMap.h` · `Containers/TLockFreeRingBuffer.h` · `Containers/TSortedVectorMap.h` · `Containers/FFastTagBitmask.h` · `Containers/FFastTagDictionary.h`
> **线程**：`TLockFreeRingBuffer` 为无锁 MPMC；`FFastTagDictionary` 以读写锁保护；`TSlotMap`、`TSortedVectorMap`、`FFastTagBitmask` 均非线程安全
> **失败语义**：无错误码，以 `bool` / `nullptr` / `INDEX_NONE` 返回；`TSlotMap::operator[]`、`TSortedVectorMap` 只读 `operator[]` 在句柄或键无效时触发 `checkf` 断言

## 30 秒速览

- **代际槽位表**：`TSlotMap<TValue, TTag>` 以稀疏槽位 + 密集数组实现 O(1) 增删查，`TGameHandle<TTag>` 过期后 `Find` 返回 `nullptr`。
- **无锁队列**：`TLockFreeRingBuffer<T, Capacity>` 基于 Vyukov 序列号 MPMC 算法，`Capacity` 必须为大于 1 的 2 的整次幂；入队出队非阻塞、满/空返回 `false`。
- **小字典**：`TSortedVectorMap<Key, Value, InlineCapacity>` 面向 ≤16 项小集合，连续内存按 Key 升序排列，查找 O(log N)，默认 `TInlineAllocator<16>`。
- **状态位**：`FFastTagBitmask` 为 8 字节纯值类型，位索引仅接受 `[0, 63]`；`FFastTagDictionary` 提供 `FName` 与位索引的双向映射，最多 64 个标签。
- **约束**：`TSlotMap::ForEach` 遍历期间禁止结构性修改（内部 `checkf` 拦截）；`TLockFreeRingBuffer` 禁止在调用栈上直接分配大容量实例。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `TSlotMap::Insert` | 插入元素并签发代际句柄 | 非线程安全；`ForEach` 期间调用触发 `checkf` |
| `TSlotMap::Find` | 按句柄取元素指针 | 无效或过期句柄返回 `nullptr`；下一次结构性变更后指针失效 |
| `TSlotMap::Remove` | 按句柄移除元素 | 过期句柄返回 `false`；采用尾部交换弹出 |
| `TLockFreeRingBuffer::Enqueue` | 非阻塞入队 | 无锁 MPMC；队列满返回 `false` |
| `TLockFreeRingBuffer::Dequeue` | 非阻塞出队 | 队列空返回 `false`；拷贝/移动构造被删除 |
| `TSortedVectorMap::Add` | 按键插入或覆盖值 | 非线程安全；面向 ≤16 项小集合 |
| `TSortedVectorMap::Find` | 按键查找值指针 | 只读 `operator[]` 在键不存在时触发 `checkf` |
| `FFastTagBitmask::HasAny` | 判断掩码是否存在交集 | 位索引仅接受 `[0, 63]`，越界返回 `false` |
| `FFastTagDictionary::RegisterOrFindTag` | 注册标签并返回位索引 | 最多 64 个标签，注册满返回 `INDEX_NONE` |
| `FFastTagDictionary::MakeMask` | 批量标签名聚合为掩码 | 任一标签非法时 `OutAllSucceeded` 为 `false` |

## 1. 代际槽位表 `TSlotMap<TValue, TTag>`

```cpp
template <typename TValue, typename TTag = TValue>
class TSlotMap {
public:
    using ValueType = TValue;
    using HandleType = TGameHandle<TTag>;
    using SizeType = int32;

    void Reserve(SizeType Capacity);

    [[nodiscard]] HandleType Insert(ValueType InValue);

    template <typename... TArgs>
    [[nodiscard]] HandleType Emplace(TArgs&&... Args);

    bool Remove(HandleType Handle);
    [[nodiscard]] bool IsValidHandle(HandleType Handle) const noexcept;

    [[nodiscard]] ValueType* Find(HandleType Handle) noexcept;
    [[nodiscard]] const ValueType* Find(HandleType Handle) const noexcept;
    [[nodiscard]] ValueType& operator[](HandleType Handle);
    [[nodiscard]] const ValueType& operator[](HandleType Handle) const;

    void Reset();
    [[nodiscard]] SizeType Num() const noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;

    [[nodiscard]] TArrayView<const ValueType> GetDenseValues() const noexcept;
    [[nodiscard]] TArrayView<ValueType> GetMutableDenseValues() noexcept;
    [[nodiscard]] HandleType GetHandleForDenseIndex(SizeType DenseIndex) const noexcept;

    template <typename TFunc> void ForEach(TFunc&& Func);
    template <typename TFunc> void ForEach(TFunc&& Func) const;
};
```

| 方法 | 参数 | 返回 |
| :--- | :--- | :--- |
| `Insert` | 按值接收并移动入槽的元素 | 新签发的 `TGameHandle<TTag>` |
| `Emplace` | 转发给元素构造函数的参数包 | 新签发的 `TGameHandle<TTag>` |
| `Remove` | 目标句柄 | 有效且移除成功 `true`；句柄无效或过期 `false` |
| `Find` | 目标句柄 | 命中元素指针；无效/过期 `nullptr` |
| `IsValidHandle` | 目标句柄 | 指向活跃且代际匹配的元素返回 `true` |
| `ForEach` | `void(Handle, Value&)` 或 `bool(Handle, Value&)` | 无；回调返回 `false` 提前终止 |
| `GetHandleForDenseIndex` | 密集数组索引 `[0, Num() - 1]` | 对应句柄；越界返回默认无效句柄 |
| `Num` / `IsEmpty` / `Reset` | 无 | 活跃元素数 / 是否为空 / 清空并使全部既有句柄失效 |

- **线程**：非线程安全，仅限单线程使用；容器内部以 `IteratingDepth` 计数拦截 `ForEach` 期间的结构性修改（`Insert` / `Emplace` / `Remove` / `Reset` / 拷贝或移动赋值均触发 `checkf`）。
- **生命周期**：`Find` / `GetDenseValues` 返回的指针或视图在下一次结构性变更后失效，严禁跨变更缓存。
- **失败语义**：`Remove` 对过期句柄返回 `false`；`operator[]` 在 `Find` 失败时触发断言；槽位代际达到 `MAX_uint32` 后槽位置 0 并永久退役，不会进入复用链表。
- **内存**：`Remove` 采用尾部交换弹出（Swap & Pop），密集元素数组保持连续；`Reserve` 仅预分配，不改变 `Num()`。

### 最小示例

```cpp
struct FEnemyTag {};
TSlotMap<FVector, FEnemyTag> EnemyPositions;

const auto Handle = EnemyPositions.Insert(FVector(100.f, 0.f, 0.f));
if (FVector* Pos = EnemyPositions.Find(Handle)) {
    Pos->Z += 10.f;
}

EnemyPositions.ForEach([](TGameHandle<FEnemyTag> CurHandle, FVector& CurPos) {
    CurPos.X += 1.f;
});

EnemyPositions.Remove(Handle); // 原句柄立即失效，Find 返回 nullptr
```

## 2. 无锁 MPMC 环形缓冲区 `TLockFreeRingBuffer<T, Capacity>`

```cpp
template <typename T, uint32 Capacity = 1024>
class TLockFreeRingBuffer {
    static_assert((Capacity > 1) && ((Capacity & (Capacity - 1)) == 0), "Capacity must be a power of two");

public:
    using ElementType = T;

    bool Enqueue(const T& Item);
    bool Enqueue(T&& Item);

    template <typename... Args>
    bool Emplace(Args&&... InArgs);

    bool Dequeue(T& OutItem);
    [[nodiscard]] TOptional<T> Dequeue();

    void Clear() noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] uint32 Num() const noexcept;
    [[nodiscard]] static constexpr uint32 GetCapacity() noexcept;
};
```

| 方法 | 参数 | 返回 |
| :--- | :--- | :--- |
| `Enqueue` | 左值拷贝或右值移动的元素 | 入队成功 `true`；队列满 `false` |
| `Emplace` | 元素构造参数包 | 入队成功 `true`；队列满 `false` |
| `Dequeue(T&)` | 接收出队元素的引用 | 出队成功 `true`；队列空 `false` |
| `Dequeue()` | 无 | 含元素的 `TOptional<T>`；队列空返回未设置值 |
| `Clear` | 无 | 弹出并析构全部现存元素，建议在无并发生产/消费时调用 |
| `Num` | 无 | `[0, Capacity]` 的近似元素数（弱一致性） |
| `IsEmpty` / `GetCapacity` | 无 | 是否为空 / 编译期固定容量 |

- **线程**：多生产者多消费者安全；`Head` / `Tail` 与槽位按缓存行对齐，入队出队全程无互斥锁。
- **生命周期**：拷贝构造、拷贝赋值、移动构造、移动赋值均被删除；析构函数会 `Clear()` 并析构队列中剩余元素。
- **失败语义**：`Enqueue` / `Dequeue` 均为非阻塞，队列满或空直接返回 `false`，不等待、不丢弃已有元素。
- **内存**：内部持有定长内联数组 `FSlot Slots[Capacity]`；默认容量 1024 或载荷较大时对象可达数十 KB，严禁在局部调用栈直接分配，建议作为类成员、单例或 `MakeUnique` 堆分配持有。
- **弱一致性**：`IsEmpty` / `Num` 仅比较游标，正在构造或析构的瞬态槽位不计入。

### 最小示例

```cpp
TUniquePtr<TLockFreeRingBuffer<int32, 1024>> EventQueue =
    MakeUnique<TLockFreeRingBuffer<int32, 1024>>();

// 生产者线程：非阻塞，队列满返回 false
EventQueue->Enqueue(42);

// 消费者线程：非阻塞，队列空返回 false
int32 PoppedValue = 0;
if (EventQueue->Dequeue(PoppedValue)) {
    ProcessEvent(PoppedValue);
}
```

## 3. 连续内存扁平排序字典 `TSortedVectorMap<Key, Value, InlineCapacity>`

```cpp
template <typename KeyType, typename ValueType, int32 InlineCapacity = 16>
class TSortedVectorMap {
public:
    struct FEntry {
        KeyType Key;
        ValueType Value;

        template <typename K, typename V>
        FEntry(K&& InKey, V&& InValue);

        [[nodiscard]] bool operator==(const FEntry& Other) const;
    };

    void Reserve(int32 Capacity);

    [[nodiscard]] ValueType* Find(const KeyType& Key) noexcept;
    [[nodiscard]] const ValueType* Find(const KeyType& Key) const noexcept;
    [[nodiscard]] bool Contains(const KeyType& Key) const noexcept;

    bool Add(const KeyType& Key, const ValueType& Value);
    bool Add(KeyType&& Key, ValueType&& Value);
    ValueType& FindOrAdd(const KeyType& Key);
    ValueType& operator[](const KeyType& Key);
    const ValueType& operator[](const KeyType& Key) const;

    void AppendOrAssign(TConstArrayView<FEntry> NewEntries);
    bool Remove(const KeyType& Key);

    [[nodiscard]] int32 LowerBoundIndex(const KeyType& Key) const noexcept;
    [[nodiscard]] int32 UpperBoundIndex(const KeyType& Key) const noexcept;

    template <typename TFunc>
    void ForRange(const KeyType& MinKey, const KeyType& MaxKey, TFunc&& Func);
    template <typename TFunc>
    void ForRange(const KeyType& MinKey, const KeyType& MaxKey, TFunc&& Func) const;

    [[nodiscard]] int32 Num() const noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
    void Reset();
    void Empty();

    template <typename TFunc> void ForEach(TFunc&& Func);
    template <typename TFunc> void ForEach(TFunc&& Func) const;
};
```

| 方法 | 参数 | 返回 |
| :--- | :--- | :--- |
| `Add` | 键与值（拷贝或移动重载） | 新插入 `true`；覆盖旧值 `false` |
| `Find` / `Contains` | 键 | 命中值指针 / 是否存在 |
| `FindOrAdd` | 键 | 已有或默认构造插入后的值引用 |
| `Remove` | 键 | 移除成功 `true`；未找到 `false` |
| `LowerBoundIndex` | 基准键 | 第一个 `>= Key` 的索引；全部更小返回 `Num()` |
| `UpperBoundIndex` | 基准键 | 第一个 `> Key` 的索引；全部更小或相等返回 `Num()` |
| `ForRange` | 闭区间 `[MinKey, MaxKey]` 与遍历闭包 | 无；闭包返回 `false` 提前终止 |
| `AppendOrAssign` | 新键值对数组视图 | 无；按键去重，后出现项覆盖先出现项 |
| `ForEach` | 遍历闭包 | 按 Key 升序遍历；闭包返回 `false` 提前终止 |

- **适用规模**：推荐 ≤16 项的小集合（属性修饰、信道映射、小配置表）；`InlineCapacity` 默认 16，小数据量下零堆分配。
- **线程**：非线程安全；连续内存遍历对 CPU 预取友好，支持 C++ 范围 for（按 Key 升序）与 `ForEach` 早退，但结构变更会使已持有的元素指针失效。
- **失败语义**：只读 `operator[]` 在键不存在时触发 `checkf`；`ForRange` 在 `MaxKey < MinKey` 或空容器时直接返回。
- **自引用保护**：`AppendOrAssign` 的输入视图若落在自身存储区间内会被静默忽略，避免搬移期间读写重叠。

### 最小示例

```cpp
TSortedVectorMap<int32, float, 8> Modifiers;

Modifiers[1] = 1.25f;
Modifiers[3] = 0.80f;

if (const float* Mod = Modifiers.Find(1)) {
    UE_LOG(LogTemp, Log, TEXT("Modifier: %.2f"), *Mod);
}

Modifiers.ForRange(1, 2, [](const int32& Key, const float& Val) {
    UE_LOG(LogTemp, Log, TEXT("In Range: Key=%d, Val=%.2f"), Key, Val);
});

using FEntry = TSortedVectorMap<int32, float, 8>::FEntry;
const FEntry ExtraBatch[] = { FEntry(1, 1.50f), FEntry(5, 2.0f) };
Modifiers.AppendOrAssign(MakeArrayView(ExtraBatch, 2));
```

## 4. 状态位掩码 `FFastTagBitmask` 与标签字典 `FFastTagDictionary`

```cpp
struct GAMECORE_API FFastTagBitmask {
    uint64 Value = 0ULL;

    constexpr explicit FFastTagBitmask(uint64 InValue);

    constexpr bool AddTag(int32 BitIndex) noexcept;
    constexpr bool RemoveTag(int32 BitIndex) noexcept;
    constexpr bool ToggleTag(int32 BitIndex) noexcept;
    [[nodiscard]] constexpr bool HasTag(int32 BitIndex) const noexcept;

    [[nodiscard]] constexpr bool HasAny(const FFastTagBitmask& Other) const noexcept;
    [[nodiscard]] constexpr bool HasAll(const FFastTagBitmask& Other) const noexcept;
    [[nodiscard]] constexpr bool HasNone(const FFastTagBitmask& Other) const noexcept;

    constexpr void Clear() noexcept;
    [[nodiscard]] constexpr bool IsEmpty() const noexcept;
    [[nodiscard]] int32 CountBits() const noexcept;

    [[nodiscard]] constexpr FFastTagBitmask operator|(const FFastTagBitmask& Other) const noexcept;
    [[nodiscard]] constexpr FFastTagBitmask operator&(const FFastTagBitmask& Other) const noexcept;
    [[nodiscard]] constexpr FFastTagBitmask operator^(const FFastTagBitmask& Other) const noexcept;
    [[nodiscard]] constexpr FFastTagBitmask operator-(const FFastTagBitmask& Other) const noexcept;
    [[nodiscard]] constexpr FFastTagBitmask operator~() const noexcept;
    constexpr FFastTagBitmask& operator|=(const FFastTagBitmask& Other) noexcept;
    constexpr FFastTagBitmask& operator&=(const FFastTagBitmask& Other) noexcept;
    constexpr FFastTagBitmask& operator^=(const FFastTagBitmask& Other) noexcept;
    constexpr FFastTagBitmask& operator-=(const FFastTagBitmask& Other) noexcept;

    [[nodiscard]] constexpr bool operator==(const FFastTagBitmask& Other) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const FFastTagBitmask& Other) const noexcept = default;
};

class GAMECORE_API FFastTagDictionary {
public:
    static constexpr int32 MaxTagCount = 64;

    static FFastTagDictionary& Get();

    int32 RegisterOrFindTag(FName TagName);
    [[nodiscard]] int32 FindTagIndex(FName TagName) const;
    [[nodiscard]] FName FindTagName(int32 BitIndex) const;
    [[nodiscard]] FFastTagBitmask MakeMask(TConstArrayView<FName> TagNames, bool* OutAllSucceeded = nullptr);
    [[nodiscard]] int32 Num() const;
    void Reset();
};
```

| 方法 | 参数 | 返回 |
| :--- | :--- | :--- |
| `AddTag` / `RemoveTag` / `ToggleTag` | 位索引 | 索引在 `[0, 63]` 内操作成功 `true`；越界 `false` |
| `HasTag` | 位索引 | 该位激活 `true`；越界或未激活 `false` |
| `HasAny` / `HasAll` / `HasNone` | 目标掩码 | 交集 / 子集 / 互斥判定结果 |
| `CountBits` / `IsEmpty` / `Clear` | 无 | 激活位数 / 是否为空 / 清空全部位 |
| `operator` 重载 | 目标掩码 | 按位或 / 与 / 异或对称差 / 差集 / 取反 |
| `RegisterOrFindTag` | 标签名 | 位索引 `[0, 63]`；注册满或非法名称返回 `INDEX_NONE` |
| `FindTagIndex` | 标签名 | 已注册索引；未注册 `INDEX_NONE` |
| `FindTagName` | 位索引 | 已注册名称；越界或未注册 `NAME_None` |
| `MakeMask` | 标签名视图与可选成功标志 | 聚合掩码；任一标签非法或超出 64 上限时 `OutAllSucceeded` 为 `false` |

- **性能**：单条位运算完成包含与互斥判定（历史 Development 构建参考值 ≤ 0.3 ns），结构固定 8 字节、零堆分配，支持 `operator<<` 序列化与 `GetTypeHash`。
- **线程**：`FFastTagBitmask` 是纯值类型，可自由拷贝；`FFastTagDictionary` 为函数局部静态单例，内部以 `FRWLock` 分离读写。
- **生命周期**：`FFastTagDictionary::Reset` 仅用于单元测试或环境重置，会清空全部注册信息。
- **跨进程一致性**：联机同步场景须在模块启动期按固定顺序预注册标签，确保各端位索引一致；单个字典最多 64 个标签。

### 最小示例

```cpp
FFastTagDictionary& Dict = FFastTagDictionary::Get();
const int32 StunTag = Dict.RegisterOrFindTag(TEXT("State.Combat.Stun"));
const int32 ArmorTag = Dict.RegisterOrFindTag(TEXT("State.Combat.SuperArmor"));

FFastTagBitmask CurrentState;
CurrentState.AddTag(StunTag);

FFastTagBitmask ImmunityMask;
ImmunityMask.AddTag(ArmorTag);

if (CurrentState.HasNone(ImmunityMask)) {
    UE_LOG(LogTemp, Log, TEXT("当前状态不含任何免疫标签"));
}
```

## 常见错误与边界

- **遍历中改容器**：`TSlotMap::ForEach` 回调内调用 `Insert` / `Remove` / `Reset` 或赋值会触发 `checkf`；需先收集句柄，遍历结束后再修改。
- **缓存槽位指针**：`TSlotMap::Find` / `GetDenseValues` 的指针在任意结构性变更后失效，跨帧缓存会读到悬垂内存。
- **栈上放大队列**：`TLockFreeRingBuffer` 将 `Slots[Capacity]` 内联在对象内，在栈上构造大容量实例易栈溢出。
- **非法容量**：`TLockFreeRingBuffer` 的 `Capacity` 非 2 的整次幂或 ≤1 时在编译期 `static_assert` 失败。
- **只读下标越界**：`TSortedVectorMap::operator[] const` 在键不存在时断言，需要探测请改用 `Find` / `Contains`。
- **标签越界与耗尽**：`FFastTagBitmask::AddTag(64)` 返回 `false` 而非断言；`FFastTagDictionary` 注册超过 64 项后 `RegisterOrFindTag` 返回 `INDEX_NONE`。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [快速上手](../Guides/QuickStart.md)
- [性能基准](../Architecture/Performance.md)
- [类型与句柄 API](RandomAndTypes.md)
