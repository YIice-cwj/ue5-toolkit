# GameCore 高性能与并发容器 API 参考

本文档汇总 `GameCore` 提供的底层高性能与无锁并发容器原语（全部位于 `Containers/` 目录）：
- `TSlotMap<TValue, TTag>`：紧凑连续内存代际槽位表
- `TLockFreeRingBuffer<T, Capacity>`：固定容量无锁多生产者多消费者 (MPMC) 环形缓冲区
- `TSortedVectorMap<KeyType, ValueType, InlineCapacity>`：小容量连续内存扁平排序字典
- `FFastTagBitmask` 与 `FFastTagDictionary`：64 位极速状态位掩码与标签字典

---

## 1. 紧凑连续内存代际槽位表 `TSlotMap<TValue, TTag>`

头文件：`#include "Containers/TSlotMap.h"`

### 核心特性
- **$O(1)$ 代际安全**：通过稀疏槽位数组 + 密集元素数组，每次删除进行尾部交换（Swap & Pop），保持密集元素内存绝对紧凑；
- **代际防悬垂**：生成 64 位 `TGameHandle<TTag>`，若槽位被复用或容器被 `Reset()`，过期句柄查找 100% 返回 `nullptr`；
- **缓存友好切片**：提供 `GetDenseValues()` 与 `GetMutableDenseValues()`，可直接将元素作为连续数组进行高速 CPU 缓存命中遍历；
- **代际上限退役保护**：槽位代际达到 `MAX_uint32` 时永久退役，杜绝代际环绕引发的 ABA 隐患。

```cpp
struct FEnemyTag {};
TSlotMap<FVector, FEnemyTag> EnemyPositions;

const auto Handle = EnemyPositions.Insert(FVector(100.f, 0.f, 0.f));
if (FVector* Pos = EnemyPositions.Find(Handle)) {
    Pos->Z += 10.f;
}

// 遍历密集连续数组
EnemyPositions.ForEach([](TGameHandle<FEnemyTag> CurHandle, FVector& CurPos) {
    CurPos.X += 1.f;
});

EnemyPositions.Remove(Handle); // 原句柄立即失效
```

---

## 2. 无锁 MPMC 环形缓冲区 `TLockFreeRingBuffer<T, Capacity>`

头文件：`#include "Containers/TLockFreeRingBuffer.h"`

### 核心特性与算法
- **Vyukov 序列号无锁算法**：采用 Dmitry Vyukov 的多生产者多消费者 (MPMC) 序列号协调机制，入队出队全程零互斥锁；
- **伪共享隔离**：`Head`、`Tail` 与槽位均按 `PLATFORM_CACHE_LINE_SIZE` 对齐，彻底消除多核心 False Sharing 缓存颠簸；
- **非平凡对象生命周期管理**：槽位内使用 `TTypeCompatibleBytes<T>` 配合 placement new 与显式析构，入队出队严格管理对象构造与析构。

> [!WARNING]
> **栈溢出与内存警示**：
> 本类内部持有定长内联数组 `FSlot Slots[Capacity]`。默认容量（1024）或载荷类型 `T` 较大时，对象体积可达数十 KB 以上。
> **严禁在局部调用栈上直接分配大容量实例**，强烈建议将其作为类成员变量、单例或通过 `MakeUnique` 在堆上分配持有。

```cpp
// 推荐作为类成员或堆分配持有
TUniquePtr<TLockFreeRingBuffer<int32, 1024>> EventQueue = MakeUnique<TLockFreeRingBuffer<int32, 1024>>();

// 生产者线程（非阻塞，队列满返回 false）
EventQueue->Enqueue(42);

// 消费者线程（非阻塞，队列空返回 false）
int32 PoppedValue = 0;
if (EventQueue->Dequeue(PoppedValue)) {
    // 处理出队数据
}
```

---

## 3. 连续内存扁平排序字典 `TSortedVectorMap<Key, Value, InlineCapacity>`

头文件：`#include "Containers/TSortedVectorMap.h"`

### 适用场景与优势
- **专为小集合设计**（推荐 $\le 16$ 项，如属性修饰项、信道映射、小配置表）；
- **零堆分配**：默认采用 `TInlineAllocator<16>` 预留栈容量，小数据量下彻底消除 `TMap` 哈希桶节点分配开销；
- **二分快速检索**：内部元素按 `Key` 严格升序排列，检索复杂度为 $O(\log N)$；
- **连续内存遍历**：提供极致的 CPU 数据预取优势，支持 C++ 范围 for 循环与 `ForEach` 早退；
- **下标运算符支持**：提供 `Map[Key] = Val` 与只读 `ConstMap[Key]`（自然语法，无哈希开销）；
- **二分区间与范围查询**：开放 `LowerBoundIndex` / `UpperBoundIndex`，提供 `ForRange(MinKey, MaxKey, Func)` 以 $O(\log N + K)$ 极速遍历闭区间；
- **批量合并与单趟排序去重**：`AppendOrAssign(NewEntries)`，单趟完成时间顺序稳定排序与新值覆盖去重，大幅优化多次调用 `Add()` 的连续搬移开销。

```cpp
TSortedVectorMap<int32, float, 8> Modifiers;
// 1. 自然下标访问
Modifiers[1] = 1.25f;
Modifiers[3] = 0.80f;

if (const float* Mod = Modifiers.Find(1)) {
    // 命中键 1
}

// 2. 极速区间查询（仅遍历键在 [1, 2] 闭区间范围内的条目）
Modifiers.ForRange(1, 2, [](const int32& Key, const float& Val) {
    UE_LOG(LogTemp, Log, TEXT("In Range: Key=%d, Val=%.2f"), Key, Val);
});

// 3. 批量合并追加（时序稳定覆盖）
using FEntry = TSortedVectorMap<int32, float, 8>::FEntry;
const FEntry ExtraBatch[] = { FEntry(1, 1.50f), FEntry(5, 2.0f) };
Modifiers.AppendOrAssign(MakeArrayView(ExtraBatch, 2));

// 4. 严格按 Key 升序遍历
for (const auto& Entry : Modifiers) {
    UE_LOG(LogTemp, Log, TEXT("Key:%d, Value:%.2f"), Entry.Key, Entry.Value);
}
```

---

## 4. 64 位极速状态位掩码 `FFastTagBitmask` 与标签字典 `FFastTagDictionary`

头文件：`#include "Containers/FFastTagBitmask.h"`

### 适用场景与优势
- **极速判定**：单条位运算指令完成状态包含与互斥检测（$\le 0.3\text{ns}$），适合战斗状态机、连招打断、霸体与控制互斥等高频判定；
- **8 字节紧凑结构**：纯 64 位整数，零堆内存分配，支持 `operator<<` 序列化与 `GetTypeHash`；
- **完整集合运算符**：支持或（`|`）、与（`&`）、异或对称差（`^`）、差集（`-`，扣除目标掩码全部标签）及复合赋值运算符；
- **动态字典映射 (`FFastTagDictionary`)**：线程安全单例字典，提供 `FName` 到 `[0, 63]` 位索引的双向映射。跨网络同步时，建议在模块启动期按固定顺序预注册标签。

```cpp
FFastTagDictionary& Dict = FFastTagDictionary::Get();
const int32 StunTag = Dict.RegisterOrFindTag(TEXT("State.Combat.Stun"));
const int32 ArmorTag = Dict.RegisterOrFindTag(TEXT("State.Combat.SuperArmor"));

FFastTagBitmask CurrentState;
CurrentState.AddTag(StunTag);

FFastTagBitmask ImmunityMask;
ImmunityMask.AddTag(ArmorTag);

// 互斥检测
if (CurrentState.HasNone(ImmunityMask)) {
    // 当前状态不包含任何免疫标签
}
```
