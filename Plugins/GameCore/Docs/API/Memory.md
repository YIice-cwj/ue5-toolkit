# GameCore 内存分配与字节流 API 参考

> **一句话**：线性分配器、定长块对象池、零拷贝二进制切片流与作用域刮擦内存的公共 API。
> **头文件**：按子域包含，见各节头文件行 · **线程**：`FLinearArena` / `TFixedBlockAllocator` / `TFixedSizePool` 非线程安全；`FScopedFrameScratchpad` 绑定线程局部 Arena，仅限创建它的线程栈 · **失败语义**：底层分配接口以 `nullptr` 表示失败，`TScratchArray` 分配失败触发断言；`FByteSpanWriter` / `FByteSpanReader` 使用黏性错误 `HasError()` / `IsOk()`

## 30 秒速览

- **线性分配**：`FLinearArena` 单向 bump 分配，分块链表保证已分配指针稳定；`Reset()` 执行析构并回收游标，但保留块内存。
- **作用域回退**：`CreateScopeMarker()` 返回 RAII 标记，析构时按构造逆序析构非平凡对象并回退游标。
- **定长复用**：`TFixedBlockAllocator<BlockSize, BlocksPerChunk, Alignment>` 用侵入式空闲链表做 O(1) 申请/归还；`TFixedSizePool<T>` 在其上封装 `New` / `Free`。
- **二进制切片流**：`FByteSpanWriter` / `FByteSpanReader` 在 `TArrayView` 上零堆分配编解码，支持 VarInt / ZigZag 与黏性错误。
- **单帧刮擦**：`FScopedFrameScratchpad` 绑定线程局部 Arena；`TScratchArray<T>` 在其上提供可扩容临时数组。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `FLinearArena::Allocate` | 单向 bump 分配原始字节 | 非线程安全；`InSize == 0` 或对齐非法返回 `nullptr` |
| `FLinearArena::New<T>` | 分配并构造对象 | 非线程安全；非平凡类型自动登记逆序析构 |
| `FLinearArena::CreateScopeMarker` | 捕获游标，返回 RAII 回退标记 | 必须 LIFO 析构；父标记先回退后子标记退化为空操作 |
| `FLinearArena::Reset` | 执行全部析构并回收游标 | 此前所有指针失效；未销毁标记安全失效 |
| `TFixedBlockAllocator::Allocate` | 取定长块，O(1) | 仅限单线程；系统内存耗尽返回 `nullptr` |
| `TFixedSizePool<T>::New` | 池上构造对象 | 仅限单线程，无内部锁 |
| `FByteSpanWriter::Write` | 写入平凡可复制类型 | 非线程安全；仅接受 `std::is_trivially_copyable_v<T>` |
| `FByteSpanReader::Read` | 读取平凡可复制类型 | 非线程安全；容量不足返回 `false` 并置错误态 |
| `FByteSpanWriter::IsOk` | 查询黏性错误状态 | 一旦置错，需 `ClearError()` 或 `Reset()` 恢复 |
| `TScratchArray<T>::Emplace` | 作用域临时数组追加元素 | 容量不足翻倍扩容；不得脱离 `FScopedFrameScratchpad` 作用域或跨线程 |

## 线性分配器 `FLinearArena`

头文件：`#include "Memory/FLinearArena.h"`

```cpp
class GAMECORE_API FLinearArena {
public:
    static constexpr size_t DEFAULT_CHUNK_SIZE = 64 * 1024;
    static constexpr size_t MIN_CHUNK_SIZE = 1024;

    explicit FLinearArena(size_t InDefaultChunkSize = DEFAULT_CHUNK_SIZE);
    ~FLinearArena();

    [[nodiscard]] void* Allocate(size_t InSize, size_t InAlignment = alignof(std::max_align_t));

    template <typename T, typename... TArgs>
    [[nodiscard]] T* New(TArgs&&... Args);

    template <typename T>
    [[nodiscard]] T* AllocateArray(size_t InCount);

    template <typename T, typename... Args>
    [[nodiscard]] T* NewArray(size_t InCount, Args&&... InArgs);

    void Reset();
    [[nodiscard]] FScopeMarker CreateScopeMarker();
    [[nodiscard]] int32 GetActiveScopeCount() const noexcept;
    [[nodiscard]] size_t GetTotalAllocatedBytes() const noexcept;
    [[nodiscard]] size_t GetTotalCapacityBytes() const noexcept;
};
```

| 接口 | 参数 | 返回 / 行为 |
| :--- | :--- | :--- |
| `Allocate` | `InSize` 字节数，`InAlignment` 2 的幂 | 返回块首地址；`InSize == 0` 或对齐非法返回 `nullptr` |
| `New<T>` | 转发至 `T` 构造函数 | placement new 构造；非平凡析构类型自动登记逆序析构 |
| `AllocateArray<T>` | `InCount` 元素数 | 只分配不构造；`InCount == 0` 或 `InCount > SIZE_MAX / sizeof(T)` 返回 `nullptr` |
| `NewArray<T>` | `InCount` + 元素构造实参 | 分配并逐个构造；非平凡析构类型整组逆序析构 |
| `CreateScopeMarker` | 无 | 捕获当前游标，返回 RAII 标记 |
| `Reset` | 无 | 执行全部析构、重置游标，并令此前未销毁的标记安全失效 |

- **线程**：非线程安全，跨线程请使用各自独立的 `FLinearArena` 实例。
- **生命周期**：扩容采用分段块链表，已有块不迁移，先前分配的指针与引用保持有效，直到 `Reset()` 或作用域标记回退越过它们。
- **回退契约**：`FScopeMarker` 必须按 LIFO 顺序析构；父标记或 `Reset` 先行回退后，子标记析构退化为空操作。
- **失败语义**：分配失败返回 `nullptr`；`Allocate` / `AllocateArray` 不调用构造与析构函数，生命周期由调用方管理。
- **性能**：典型分配耗时 1~3 ns（历史 Development 构建参考值），块大小下限钳制为 `MIN_CHUNK_SIZE`（1024 字节）。

```cpp
FLinearArena Arena(64 * 1024);

void* RawBuffer = Arena.Allocate(128, alignof(uint64));

struct FMonsterData {
    FString Name;
    TArray<int32> BuffIds;
};
FMonsterData* Monster = Arena.New<FMonsterData>();
Monster->Name = TEXT("Dragon");

{
    auto Marker = Arena.CreateScopeMarker();
    int32* TempValues = Arena.AllocateArray<int32>(512);
    (void)TempValues;
}

Arena.Reset();
```

## 定长块分配器 `TFixedBlockAllocator` 与对象池 `TFixedSizePool`

头文件：`#include "Memory/TFixedBlockAllocator.h"`、`#include "Memory/TFixedSizePool.h"`

```cpp
template <size_t BlockSize, size_t BlocksPerChunk = 64, size_t Alignment = alignof(void*)>
class TFixedBlockAllocator {
public:
    void* Allocate();
    void Deallocate(void* Ptr);
    bool ReserveChunks(size_t TargetChunkCount);
    void Reset();
    [[nodiscard]] size_t GetAllocatedCount() const noexcept;
    [[nodiscard]] size_t GetChunkCount() const noexcept;
    [[nodiscard]] size_t GetTotalCapacity() const noexcept;
    [[nodiscard]] static constexpr size_t GetAlignedBlockSize() noexcept;
};

template <typename T, size_t BlocksPerChunk = 64>
class TFixedSizePool {
public:
    template <typename... ArgsType>
    [[nodiscard]] T* New(ArgsType&&... Args);
    void Free(T* ObjectPtr);
    void ReserveChunks(size_t ChunkCount);
    void Reset();
    [[nodiscard]] size_t GetAllocatedCount() const noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] size_t GetChunkCount() const noexcept;
    [[nodiscard]] size_t GetTotalCapacity() const noexcept;
};
```

- **线程**：仅限单线程使用，无内部锁。
- **复用机制**：空闲块直接复用前 `sizeof(void*)` 字节存放 `Next` 指针，元数据零额外开销；申请取头指针、归还头插，均为 O(1)。
- **失败语义**：`Allocate` 在系统内存耗尽时返回 `nullptr`；`Deallocate(nullptr)` 与 `Free(nullptr)` 被忽略；`Deallocate` 在 `AllocatedCount == 0` 时触发 `checkf`（重复归还保护）。
- **容量**：`ReserveChunks` 预热大块，消除热路径首次扩容抖动；`GetAlignedBlockSize()` 返回对齐后的真实单块字节数。
- **性能**：单次申请/归还约 2~3 条机器指令（历史 Development 构建参考值）。

```cpp
TFixedBlockAllocator<32, 128> BlockPool;
void* Block = BlockPool.Allocate();
BlockPool.Deallocate(Block);

struct FPathNode {
    FVector Position;
    int32 Cost = 0;
};

TFixedSizePool<FPathNode, 256> NodePool;
FPathNode* Node = NodePool.New();
Node->Position = FVector(10.f, 20.f, 0.f);
NodePool.Free(Node);
```

## 二进制切片流 `FByteSpanWriter` / `FByteSpanReader`

头文件：`#include "Memory/FByteSpanWriter.h"`、`#include "Memory/FByteSpanReader.h"`

```cpp
class GAMECORE_API FByteSpanWriter {
public:
    constexpr explicit FByteSpanWriter(TArrayView<uint8> InBuffer) noexcept;

    template <typename T> requires std::is_trivially_copyable_v<T>
    bool Write(const T& Value) noexcept;
    bool WriteBytes(const void* Data, int32 ByteCount) noexcept;
    bool WriteVarUInt32(uint32 Value) noexcept;
    bool WriteVarInt32(int32 Value) noexcept;
    bool WriteVarUInt64(uint64 Value) noexcept;
    bool WriteVarInt64(int64 Value) noexcept;
    bool WriteString(FStringView Str) noexcept;

    template <typename T> requires std::is_trivially_copyable_v<T>
    FByteSpanWriter& operator<<(const T& Value) noexcept;
    FByteSpanWriter& operator<<(FStringView Str) noexcept;
    FByteSpanWriter& operator<<(const FString& Str) noexcept;

    constexpr void Reset() noexcept;
    constexpr void ClearError() noexcept;
    [[nodiscard]] constexpr bool IsOk() const noexcept;
    [[nodiscard]] constexpr bool HasError() const noexcept;
    [[nodiscard]] constexpr int32 GetBytesWritten() const noexcept;
    [[nodiscard]] constexpr int32 GetRemainingBytes() const noexcept;
    [[nodiscard]] constexpr TArrayView<const uint8> GetWrittenSpan() const noexcept;
    [[nodiscard]] constexpr TArrayView<uint8> GetRemainingSpan() noexcept;
};

class GAMECORE_API FByteSpanReader {
public:
    constexpr explicit FByteSpanReader(TArrayView<const uint8> InBuffer) noexcept;

    template <typename T> requires std::is_trivially_copyable_v<T>
    bool Read(T& OutValue) noexcept;
    template <typename T> requires std::is_trivially_copyable_v<T>
    [[nodiscard]] bool Peek(T& OutValue) const noexcept;

    bool ReadBytes(void* OutData, int32 ByteCount) noexcept;
    bool ReadBytesView(int32 ByteCount, TArrayView<const uint8>& OutView) noexcept;
    bool ReadVarUInt32(uint32& OutValue) noexcept;
    bool ReadVarInt32(int32& OutValue) noexcept;
    bool ReadVarUInt64(uint64& OutValue) noexcept;
    bool ReadVarInt64(int64& OutValue) noexcept;
    bool ReadUtf8Span(TArrayView<const UTF8CHAR>& OutUtf8View) noexcept;
    bool ReadString(FString& OutStr) noexcept;

    template <typename T> requires std::is_trivially_copyable_v<T>
    FByteSpanReader& operator>>(T& OutValue) noexcept;
    FByteSpanReader& operator>>(FString& OutStr) noexcept;

    bool Skip(int32 ByteCount) noexcept;
    constexpr void Reset() noexcept;
    constexpr void ClearError() noexcept;
    [[nodiscard]] constexpr bool IsOk() const noexcept;
    [[nodiscard]] constexpr bool HasError() const noexcept;
    [[nodiscard]] constexpr int32 GetBytesRead() const noexcept;
    [[nodiscard]] constexpr int32 GetRemainingBytes() const noexcept;
    [[nodiscard]] constexpr bool HasRemaining() const noexcept;
    [[nodiscard]] constexpr TArrayView<const uint8> GetRemainingSpan() const noexcept;
};
```

| 接口 | 说明 |
| :--- | :--- |
| `Write` / `Read` | 仅接受 `std::is_trivially_copyable_v<T>`；`FVector3f`（12 字节）、`FVector`（24 字节）、`FRotator`、`FQuat4f` 可直接读写 |
| `WriteBytes` / `ReadBytes` | 批量原始字节；负长度、非零长度下空指针或容量不足返回 `false` 并置错误态，零长度直接成功 |
| `WriteVarUInt32` / `ReadVarUInt32` | LEB128 变长 32 位；读取端校验第 5 字节高位溢出 |
| `WriteVarInt32` / `ReadVarInt32` | ZigZag + LEB128 有符号 32 位 |
| `WriteVarUInt64` / `ReadVarUInt64` | LEB128 变长 64 位；读取端校验第 10 字节 |
| `WriteVarInt64` / `ReadVarInt64` | ZigZag + LEB128 有符号 64 位 |
| `WriteString` / `ReadString` | UTF-8 编码，带变长 32 位字节长度前缀；失败自动回滚游标 |
| `ReadBytesView` / `ReadUtf8Span` | 直接返回指向源缓冲区的视图，不发生 `memcpy` |
| `Peek` | 原位探测下一个标量，不推进读游标 |
| `IsOk` / `HasError` | 黏性错误状态；一旦置位需 `ClearError()` 或 `Reset()` 恢复 |

- **线程**：非线程安全，读写器实例不可跨线程共享。
- **生命周期**：不持有缓冲区所有权，`TArrayView` 源内存必须比读写器存活更久。
- **零堆分配**：除调用方提供的目标缓冲区外，内部不分配堆内存。
- **失败语义**：溢出或解码失败置 `bHasError` 并返回 `false`；后续操作仍会继续尝试，但 `IsOk()` 保持 `false` 直到显式清除。

```cpp
uint8 Storage[256];
FByteSpanWriter Writer(MakeArrayView(Storage, sizeof(Storage)));

const FVector3f Direction(1.0f, 0.0f, 0.0f);
Writer << static_cast<uint16>(0x55AA) << Direction << TEXT("GameEvent");
check(Writer.IsOk());

FByteSpanReader Reader(Writer.GetWrittenSpan());

uint16 Magic = 0;
if (Reader.Peek(Magic) && Magic == 0x55AA) {
    FVector3f ReadDir;
    FString ReadEvent;
    Reader >> Magic >> ReadDir >> ReadEvent;
    check(Reader.IsOk() && ReadDir == Direction);
}
```

## 作用域刮擦内存 `FScopedFrameScratchpad` 与 `TScratchArray<T>`

头文件：`#include "Memory/FScopedFrameScratchpad.h"`、`#include "Containers/TScratchArray.h"`

```cpp
class GAMECORE_API FScopedFrameScratchpad {
public:
    static FLinearArena& GetThreadLocalArena();
    static void ResetThreadLocalArena();

    explicit FScopedFrameScratchpad();
    explicit FScopedFrameScratchpad(FLinearArena& InArena);
    ~FScopedFrameScratchpad() = default;

    [[nodiscard]] void* Allocate(size_t InSize, size_t InAlignment = alignof(std::max_align_t));

    template <typename T, typename... TArgs>
    [[nodiscard]] T* New(TArgs&&... Args);

    template <typename T>
    [[nodiscard]] TArrayView<T> AllocArray(int32 Count);

    template <typename T, typename... Args>
    [[nodiscard]] TArrayView<T> NewArray(int32 Count, Args&&... InArgs);

    [[nodiscard]] FLinearArena& GetArena() noexcept;
};

template <typename T>
class TScratchArray {
public:
    explicit TScratchArray(FScopedFrameScratchpad& InScratchpad, int32 InInitialCapacity = 16);

    void Reserve(int32 InCapacity);

    template <typename... ArgsType>
    T& Emplace(ArgsType&&... Args);

    void Add(const T& Item);
    void Add(T&& Item);
    T Pop();

    [[nodiscard]] const T& operator[](int32 Index) const;
    [[nodiscard]] T& operator[](int32 Index);
    [[nodiscard]] int32 Num() const noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] int32 Capacity() const noexcept;
    [[nodiscard]] const T* GetData() const noexcept;
    [[nodiscard]] T* GetData() noexcept;
    void Reset();
    [[nodiscard]] TArrayView<T> AsView() noexcept;
    [[nodiscard]] TConstArrayView<T> AsView() const noexcept;
};
```

| 接口 | 参数 | 返回 / 行为 |
| :--- | :--- | :--- |
| `GetThreadLocalArena` | 无 | 当前线程专属 `FLinearArena` 引用 |
| `ResetThreadLocalArena` | 无 | 重置当前线程分配器，建议每帧末调用 |
| `Allocate` / `New` / `AllocArray` / `NewArray` | 同 `FLinearArena` | 失败返回 `nullptr` 或空 `TArrayView` |
| `TScratchArray` 构造 | `FScopedFrameScratchpad&`、初始容量 | 初始分配失败触发 `checkf` |
| `Emplace` / `Add` | 元素或构造实参 | 容量不足时在当前 Arena 内翻倍扩容 |
| `Pop` | 无 | 空数组触发 `check` |
| `operator[]` | `[0, Num())` | 越界触发 `check` |

- **线程**：`FScopedFrameScratchpad` 绑定线程局部 Arena（或显式传入的 Arena），仅限创建它的线程栈上使用，严禁跨线程传递或异步析构。
- **生命周期**：构造函数记录游标，析构自动回退并执行当前作用域内创建的非平凡对象析构；`TScratchArray` 的元素随外层作用域一起回收。
- **扩容**：平凡可复制类型直接 `memcpy`；非平凡类型逐元素 placement new 移动构造并析构旧元素。
- **失败语义**：`AllocArray` / `NewArray` 在 `Count <= 0` 时返回空视图；`TScratchArray` 初始分配与扩容失败均触发 `checkf`。

```cpp
void ProcessFrameBatch() {
    FScopedFrameScratchpad Scratchpad;

    TScratchArray<FVector> VisiblePoints(Scratchpad, 64);
    for (int32 i = 0; i < 100; ++i) {
        VisiblePoints.Emplace(i * 10.f, 0.f, 0.f);
    }

    CalculateBoundingBox(VisiblePoints.AsView());
}
```

> [!TIP]
> 引擎主循环应在每帧结束时调用 `FScopedFrameScratchpad::ResetThreadLocalArena()`，确保各线程保留块清零游标并备战下一帧。

## 常见错误与边界

- **指针稳定性**：`FLinearArena` 的指针只在 `Reset()` 或更外层标记回退之前有效；`Reset()` 后所有先前分配失效。
- **标记顺序**：`FScopeMarker` 必须 LIFO 析构；逆序或父级先行回退不会崩溃，但子标记退化为空操作。
- **非平凡对象**：用 `Allocate` / `AllocateArray` 分配非平凡类型时不会自动构造/析构，需改用 `New` / `NewArray` 或自行管理。
- **重复归还**：`TFixedBlockAllocator::Deallocate` 在 `AllocatedCount == 0` 时触发 `checkf`；`TFixedSizePool::Free` 会先析构再归还。
- **黏性错误**：`FByteSpanWriter` / `FByteSpanReader` 一旦置错，需调用 `ClearError()` 或 `Reset()` 才能恢复 `IsOk()`。
- **作用域越界**：`TScratchArray` 不得脱离其 `FScopedFrameScratchpad` 作用域使用，也不得跨线程共享。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [缓存与内存架构](../Architecture/CachingAndMemory.md)
- [性能基准](../Architecture/Performance.md)
- [快速上手](../Guides/QuickStart.md)
