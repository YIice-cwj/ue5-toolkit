# GameCore 高性能内存与分配器 API 参考

本文档汇总 `GameCore` 提供的底层高性能内存管理与连续流序列化原语（全部位于 `Memory/` 目录）：
- `FLinearArena` / `TLinearArena`：高速单向线性分配器与非平凡对象析构链管理
- `TFixedBlockAllocator` 与 `TFixedSizePool<T>`：固定微块侵入式空闲链表分配器与对象池
- `FByteSpanWriter` 与 `FByteSpanReader`：零拷贝连续内存二进制切片流读写器
- `FScopedFrameScratchpad` 与 `TScratchArray<T>`：作用域级单帧刮擦内存与临时连续动态数组

---

## 1. 高速单向线性分配器 `FLinearArena`

头文件：`#include "Memory/TLinearArena.h"`

### 核心特性与内存契约
- **1~3 纳秒级分配**：采用单向指针自增（Bump Allocation），跨块分配自动开辟新链表块，极速满足短期或批量内存申请；
- **指针绝对稳定**：扩容时采用分段链表块（`FMemoryChunk`）级联，**绝不迁移或重新分配已有块的内存**，先前分配的所有对象指针与引用永久有效直至被重置；
- **非平凡对象生命周期与逆序析构**：
  - 针对非平凡类型提供 `New<T>(Args...)` 与 `Emplace<T>(Args...)`；
  - 内部自动建立侵入式析构链节点（`FDestructorNode`），在 `Reset()` 或标记回滚时严格按**构造的相反顺序**依次执行析构函数；
- **作用域标记与部分回滚**：
  - 调用 `CreateScopeMarker()` 获得 RAII 守卫对象 `FScopeMarker`；
  - 作用域结束或显式回退时，仅回滚该标记之后的游标与析构对应对象，前序分配完好保留。

```cpp
FLinearArena Arena(64 * 1024); // 初始 Chunk 大小 64KB

// 分配原生对齐内存
void* RawBuffer = Arena.Allocate(128, alignof(uint64));

// 在 Arena 中构造复杂对象（自动注册析构函数）
struct FMonsterData {
    FString Name;
    TArray<int32> BuffIds;
};
FMonsterData* Monster = Arena.New<FMonsterData>();
Monster->Name = TEXT("Dragon");
Monster->BuffIds.Add(1001);

// RAII 局部回退标记
{
    auto Marker = Arena.CreateScopeMarker();
    int32* TempValues = Arena.AllocateArray<int32>(512);
    // 离开大括号，Marker 自动析构并回退游标至进入时的位置
}

// 任务完成或帧结束时全量重置（逆序析构所有残留对象，保留块内存供后续复用）
Arena.Reset();
```

---

## 2. 定长微块侵入式空闲链表分配器 `TFixedBlockAllocator`

头文件：`#include "Memory/TFixedBlockAllocator.h"`

### 适用场景与性能优势
- **极速微块复用**：针对高频创建、删除的微小对象（如节点、事件包、投射物实体元数据）；
- **侵入式单向链表（Intrusive Free-List）**：未借出的空闲块直接复用前 `sizeof(void*)` 字节作为 `Next` 指针，元数据开销为绝对零；
- **严格 $O(1)$ 指令分配与归还**：申请仅需取出头指针，归还仅需头插法插入，耗时恒定在 2~3 条机器指令；
- **类型化对象池 `TFixedSizePool<T>`**：在分配器基础上封装 `Construct(Args...)` 与 `Destroy(Ptr)`，自动管理对象的 placement new 与显式析构。

```cpp
// 1. 原生块分配器（块尺寸 32 字节，每个 Chunk 预备 128 个块）
TFixedBlockAllocator<32, 128> BlockPool;
void* Block = BlockPool.Allocate();
// ... 使用 Block ...
BlockPool.Deallocate(Block);

// 2. 类型化对象池
struct FPathNode {
    FVector Position;
    int32 Cost = 0;
};

TFixedSizePool<FPathNode, 256> NodePool;
FPathNode* Node = NodePool.Construct();
Node->Position = FVector(10.f, 20.f, 0.f);

// 归还对象（显式调用析构函数并将块插入空闲链表）
NodePool.Destroy(Node);
```

---

## 3. 零拷贝连续内存二进制切片流 `FByteSpanWriter` / `FByteSpanReader`

头文件：`#include "Memory/FByteSpanStream.h"`

### 核心特性
- **绝对零堆分配**：全程基于连续内存切片（`TArrayView<uint8>` 或 `TArrayView<const uint8>`）进行就地序列化；
- **流式操作符与黏性错误模型**：
  - 支持类似 `FArchive` 的 `Writer << A << B << C` 与 `Reader >> X >> Y >> Z` 流式链式操作；
  - 内置 **Sticky Error（黏性错误状态）**：若中间任何一环发生缓冲区溢出或解码失败，自动进入错误态（`HasError() == true`，`IsOk() == false`），允许整批流水线序列化后统一通过 `IsOk()` 校验一次，彻底消灭嵌套样板代码；
- **变长整数压缩编码（VarInt / ZigZag）**：
  - **32 位轻量变长整数**：`WriteVarUInt32` / `ReadVarUInt32`，`WriteVarInt32` / `ReadVarInt32`，专为枚举、长度前缀与 ID 优化，循环更紧凑；
  - **64 位长整数压缩**：`WriteVarUInt64` / `ReadVarUInt64`，`WriteVarInt64` / `ReadVarInt64`，配合 ZigZag 编码消除负数补码高位冗余膨胀；
- **引擎核心数学结构直接支持**：原生提供 `WriteVector3f` / `ReadVector3f`（12 字节紧凑三维向量）、`WriteVector` / `ReadVector`（24 字节双精度三维向量）、`WriteRotator` / `ReadRotator` 与 `WriteQuat4f` / `ReadQuat4f`（16 字节四元数）；
- **真正零拷贝视图与原位探测**：
  - `ReadBytesView(ByteCount, OutView)`：直接返回指向源内存切片的 `TArrayView<const uint8>`，绝不发生 `memcpy`；
  - `ReadUtf8Span(OutView)`：直接返回指向源缓冲区的 UTF-8 字节切片视图，零堆分配反序列化字符串；
  - `Peek(OutValue)`：原位探测下一个标量值，不推进读取游标。

```cpp
uint8 Storage[256];
FByteSpanWriter Writer(MakeArrayView(Storage, sizeof(Storage)));

// 1. 流式链式写入与引擎数学类型
const FVector3f Direction(1.0f, 0.0f, 0.0f);
Writer << static_cast<uint16>(0x55AA) << Direction << TEXT("GameEvent");

// 2. 校验写入健康状态
check(Writer.IsOk());

// 3. 读取与原位探测
FByteSpanReader Reader(Writer.GetWrittenSpan());

uint16 Magic = 0;
// 原位探测 Magic Header，不移动读游标
if (Reader.Peek(Magic) && Magic == 0x55AA) {
    FVector3f ReadDir;
    FString ReadEvent;
    Reader >> Magic >> ReadDir >> ReadEvent;
    check(Reader.IsOk() && ReadDir == Direction && ReadEvent == TEXT("GameEvent"));
}
```

---

## 4. 单帧局部刮擦分配器 `FScopedFrameScratchpad` 与 `TScratchArray<T>`

头文件：`#include "Memory/FScopedFrameScratchpad.h"`

### 核心设计与使用规范
- **线程局部单例（TLS）**：`FScopedFrameScratchpad::GetThreadLocalArena()` 为每个工作线程分配专属的高速单帧线性栈，避免跨线程锁竞态；
- **RAII 作用域自动清理**：构造时记录游标位置，析构时自动回滚游标并执行当前作用域内创建的非平凡对象析构函数；
- **连续动态临时数组 `TScratchArray<T>`**：
  - 提供类似 `TArray` 的直观接口（`Add`、`Emplace`、`Pop`、`Num`、`AsView` 等）；
  - 动态扩容时在当前刮擦 Arena 内分配新连续内存块，平凡可复制类型直接 memcpy，非平凡类型 placement new 移动构造并自动析构旧元素；
  - 数组生命周期随外层作用域自然销毁，无需显式释放堆内存。

```cpp
void ProcessFrameBatch() {
    // 建立刮擦内存作用域守卫
    FScopedFrameScratchpad Scratchpad;

    // 申请 64 项容量的局部临时数组（零堆分配）
    TScratchArray<FVector> VisiblePoints(Scratchpad, 64);
    for (int32 i = 0; i < 100; ++i) {
        VisiblePoints.Emplace(i * 10.f, 0.f, 0.f); // 超出容量时自动在 Arena 中平滑扩容
    }

    // 传递为 ArrayView 给纯函数计算
    CalculateBoundingBox(VisiblePoints.AsView());

    // 函数结束离开作用域：Scratchpad 析构，自动回退游标至调用前状态，彻底回收临时数组内存
}
```

> [!TIP]
> **帧末回收契约**：
> 引擎主循环应在每帧结束时调用 `FScopedFrameScratchpad::ResetThreadLocalArena()`，确保各线程的保留块完全清零游标并备战下一帧。
