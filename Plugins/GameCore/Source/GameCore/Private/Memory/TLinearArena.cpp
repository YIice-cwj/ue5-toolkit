/**
 * @file TLinearArena.cpp
 * @brief FLinearArena 与 FScopeMarker 实现
 */

#include "Memory/TLinearArena.h"

#include <utility>

namespace {

/**
 * @brief 计算内存地址或偏移沿指定对齐边界向上取整
 * @param InValue 原始值
 * @param InAlignment 对齐字节数（必须为 2 的幂）
 * @return 对齐后的数值
 */
inline size_t AlignUp(size_t InValue, size_t InAlignment) noexcept {
    return (InValue + (InAlignment - 1)) & ~(InAlignment - 1);
}

}  // namespace

/**
 * @brief 构造函数：预分配指定大小的初始内存块（下限钳制为 MIN_CHUNK_SIZE = 1024 字节）
 * @param InDefaultChunkSize 默认内存块大小（字节，默认 64KB）
 */
FLinearArena::FLinearArena(size_t InDefaultChunkSize)
    : DefaultChunkSize(FMath::Max<size_t>(InDefaultChunkSize, MIN_CHUNK_SIZE))
    , FirstChunk(new FMemoryChunk(DefaultChunkSize, nullptr))
    , CurrentChunk(FirstChunk)
    , DestructorHead(nullptr) {
}

/**
 * @brief 析构函数：执行所有析构链并释放所有内存块
 */
FLinearArena::~FLinearArena() {
    Reset();

    FMemoryChunk* Node = FirstChunk;
    while (Node != nullptr) {
        FMemoryChunk* Next = Node->NextChunk;
        delete Node;
        Node = Next;
    }

    FirstChunk = nullptr;
    CurrentChunk = nullptr;
}

/**
 * @brief 移动构造函数
 * @param Other 被移动实例
 */
FLinearArena::FLinearArena(FLinearArena&& Other) noexcept
    : DefaultChunkSize(Other.DefaultChunkSize)
    , FirstChunk(Other.FirstChunk)
    , CurrentChunk(Other.CurrentChunk)
    , DestructorHead(Other.DestructorHead)
    , NextMarkerId(Other.NextMarkerId)
    , ActiveMarkerStack(MoveTemp(Other.ActiveMarkerStack)) {
    Other.DefaultChunkSize = 0;
    Other.FirstChunk = nullptr;
    Other.CurrentChunk = nullptr;
    Other.DestructorHead = nullptr;
    Other.NextMarkerId = 0;
    Other.ActiveMarkerStack.Reset();
}

/**
 * @brief 移动赋值操作符
 * @param Other 被移动实例
 * @return 自身引用
 */
FLinearArena& FLinearArena::operator=(FLinearArena&& Other) noexcept {
    if (this != &Other) {
        Reset();

        FMemoryChunk* Node = FirstChunk;
        while (Node != nullptr) {
            FMemoryChunk* Next = Node->NextChunk;
            delete Node;
            Node = Next;
        }

        DefaultChunkSize = Other.DefaultChunkSize;
        FirstChunk = Other.FirstChunk;
        CurrentChunk = Other.CurrentChunk;
        DestructorHead = Other.DestructorHead;
        NextMarkerId = Other.NextMarkerId;
        ActiveMarkerStack = MoveTemp(Other.ActiveMarkerStack);

        Other.DefaultChunkSize = 0;
        Other.FirstChunk = nullptr;
        Other.CurrentChunk = nullptr;
        Other.DestructorHead = nullptr;
        Other.NextMarkerId = 0;
        Other.ActiveMarkerStack.Reset();
    }
    return *this;
}

/**
 * @brief 分配指定大小与对齐的连续原生内存
 * @param InSize 期望申请的字节数
 * @param InAlignment 对齐字节数（必须为 2 的幂）
 * @return 内存块起始地址，入参非法或容量为 0 时返回 nullptr
 */
void* FLinearArena::Allocate(size_t InSize, size_t InAlignment) {
    if (InSize == 0 || InAlignment == 0 || (InAlignment & (InAlignment - 1)) != 0) {
        return nullptr;
    }
    if (InSize > (SIZE_MAX - InAlignment)) {
        return nullptr;
    }

    auto ComputeAlignedOffset = [InAlignment](const FMemoryChunk* Chunk) -> size_t {
        const uintptr_t RawBase = reinterpret_cast<uintptr_t>(Chunk->Buffer);
        const uintptr_t CurrentAddress = RawBase + Chunk->Cursor;
        const uintptr_t AlignedAddress = (CurrentAddress + (InAlignment - 1)) & ~(InAlignment - 1);
        return static_cast<size_t>(AlignedAddress - RawBase);
    };

    if (CurrentChunk != nullptr) {
        const size_t AlignedOffset = ComputeAlignedOffset(CurrentChunk);
        if (AlignedOffset <= CurrentChunk->Capacity && InSize <= (CurrentChunk->Capacity - AlignedOffset)) {
            uint8* ResultPtr = CurrentChunk->Buffer + AlignedOffset;
            CurrentChunk->Cursor = AlignedOffset + InSize;
            return ResultPtr;
        }
    }

    const size_t AlignedSize = AlignUp(InSize, InAlignment);
    if (AlignedSize < InSize || AlignedSize > (SIZE_MAX - InAlignment)) {
        return nullptr;
    }
    const size_t MinimumCapacity = AlignedSize + InAlignment;
    const size_t NextChunkCapacity = FMath::Max(DefaultChunkSize, MinimumCapacity);

    if (CurrentChunk != nullptr && CurrentChunk->NextChunk != nullptr) {
        const size_t CandidateAlignedOffset = ComputeAlignedOffset(CurrentChunk->NextChunk);
        if (CandidateAlignedOffset <= CurrentChunk->NextChunk->Capacity &&
            InSize <= (CurrentChunk->NextChunk->Capacity - CandidateAlignedOffset) &&
            CurrentChunk->NextChunk->Capacity >= NextChunkCapacity) {
            CurrentChunk = CurrentChunk->NextChunk;
            CurrentChunk->Cursor = 0;
            const size_t AlignedOffset = ComputeAlignedOffset(CurrentChunk);
            uint8* ResultPtr = CurrentChunk->Buffer + AlignedOffset;
            CurrentChunk->Cursor = AlignedOffset + InSize;
            return ResultPtr;
        }

        FMemoryChunk* StaleHead = CurrentChunk->NextChunk;
        while (StaleHead != nullptr) {
            FMemoryChunk* Next = StaleHead->NextChunk;
            delete StaleHead;
            StaleHead = Next;
        }
        CurrentChunk->NextChunk = nullptr;
    }

    FMemoryChunk* NewChunk = new FMemoryChunk(NextChunkCapacity, nullptr, InAlignment);
    if (CurrentChunk != nullptr) {
        CurrentChunk->NextChunk = NewChunk;
    } else {
        FirstChunk = NewChunk;
    }
    CurrentChunk = NewChunk;

    const size_t AlignedOffset = ComputeAlignedOffset(CurrentChunk);
    uint8* ResultPtr = CurrentChunk->Buffer + AlignedOffset;
    CurrentChunk->Cursor = AlignedOffset + InSize;
    return ResultPtr;
}

/**
 * @brief 重置整个分配器状态（执行所有非平凡析构并重置游标，同时使此前未销毁的 FScopeMarker 安全失效）
 */
void FLinearArena::Reset() {
    ActiveMarkerStack.Reset();
    RewindTo(FirstChunk, 0, nullptr);
}

/**
 * @brief 创建当前游标位置的 RAII 作用域标记
 * @return 作用域标记实例
 */
FLinearArena::FScopeMarker FLinearArena::CreateScopeMarker() {
    return FScopeMarker(*this);
}

/**
 * @brief 获取当前已分配总内存大小（字节）
 * @return 已使用的字节数
 */
size_t FLinearArena::GetTotalAllocatedBytes() const noexcept {
    size_t TotalAllocated = 0;
    const FMemoryChunk* Node = FirstChunk;
    while (Node != nullptr) {
        TotalAllocated += Node->Cursor;
        if (Node == CurrentChunk) {
            break;
        }
        Node = Node->NextChunk;
    }
    return TotalAllocated;
}

/**
 * @brief 获取当前保留的内存总容量（字节）
 * @return 容量字节数
 */
size_t FLinearArena::GetTotalCapacityBytes() const noexcept {
    size_t TotalCapacity = 0;
    const FMemoryChunk* Node = FirstChunk;
    while (Node != nullptr) {
        TotalCapacity += Node->Capacity;
        Node = Node->NextChunk;
    }
    return TotalCapacity;
}

/**
 * @brief 注册非平凡析构回调节点
 * @param InDestructorFunc 析构函数指针
 * @param InObjectPtr 目标对象指针
 */
void FLinearArena::RegisterDestructor(void (*InDestructorFunc)(void*), void* InObjectPtr) {
    if (InDestructorFunc == nullptr || InObjectPtr == nullptr) {
        return;
    }

    void* NodeMemory = Allocate(sizeof(FDestructorNode), alignof(FDestructorNode));
    check(NodeMemory != nullptr);

    FDestructorNode* NewNode = static_cast<FDestructorNode*>(NodeMemory);
    NewNode->DestructorFunc = InDestructorFunc;
    NewNode->ObjectPtr = InObjectPtr;
    NewNode->NextNode = DestructorHead;
    DestructorHead = NewNode;
}

/**
 * @brief 回退游标并析构指定节点之后的对象
 * @param TargetChunk 目标块指针
 * @param TargetCursor 目标游标偏移
 * @param TargetDestructorNode 目标析构链末端节点
 */
void FLinearArena::RewindTo(FMemoryChunk* TargetChunk, size_t TargetCursor, FDestructorNode* TargetDestructorNode) {
    while (DestructorHead != TargetDestructorNode && DestructorHead != nullptr) {
        FDestructorNode* CurrentNode = DestructorHead;
        DestructorHead = CurrentNode->NextNode;
        if (CurrentNode->DestructorFunc != nullptr && CurrentNode->ObjectPtr != nullptr) {
            CurrentNode->DestructorFunc(CurrentNode->ObjectPtr);
        }
    }

    if (TargetChunk == nullptr) {
        TargetChunk = FirstChunk;
        TargetCursor = 0;
    }

    CurrentChunk = TargetChunk;
    if (CurrentChunk != nullptr) {
        CurrentChunk->Cursor = TargetCursor;

        FMemoryChunk* Subsequent = CurrentChunk->NextChunk;
        while (Subsequent != nullptr) {
            Subsequent->Cursor = 0;
            Subsequent = Subsequent->NextChunk;
        }
    }
}

/**
 * @brief 释放指定标记 ID 及建立在其后的内层标记并执行回退（若标记已因 Reset 或外层回退失效则忽略）
 * @param MarkerId 标记唯一递增 ID
 * @param TargetChunk 目标块指针
 * @param TargetCursor 目标游标偏移
 * @param TargetDestructorNode 目标析构链末端节点
 */
void FLinearArena::PopAndRewindMarker(
    uint64 MarkerId,
    FMemoryChunk* TargetChunk,
    size_t TargetCursor,
    FDestructorNode* TargetDestructorNode
) {
    if (MarkerId == 0 || ActiveMarkerStack.IsEmpty()) {
        return;
    }

    const int32 FoundIndex = ActiveMarkerStack.FindLast(MarkerId);
    if (FoundIndex == INDEX_NONE) {
        return;
    }

    ActiveMarkerStack.SetNum(FoundIndex, EAllowShrinking::No);
    RewindTo(TargetChunk, TargetCursor, TargetDestructorNode);
}

/**
 * @brief 构造函数：捕获当前分配器状态并压入活跃标记栈
 * @param InArena 所属分配器引用
 */
FLinearArena::FScopeMarker::FScopeMarker(FLinearArena& InArena)
    : ArenaPtr(&InArena)
    , SavedChunk(InArena.CurrentChunk)
    , SavedCursor(InArena.CurrentChunk ? InArena.CurrentChunk->Cursor : 0)
    , SavedDestructorNode(InArena.DestructorHead)
    , MarkerId(++InArena.NextMarkerId) {
    InArena.ActiveMarkerStack.Add(MarkerId);
}

/**
 * @brief 析构函数：回退分配器至标记捕获点
 */
FLinearArena::FScopeMarker::~FScopeMarker() {
    if (ArenaPtr != nullptr) {
        ArenaPtr->PopAndRewindMarker(MarkerId, SavedChunk, SavedCursor, SavedDestructorNode);
        ArenaPtr = nullptr;
        MarkerId = 0;
    }
}

/**
 * @brief 移动构造函数
 * @param Other 被移动标记
 */
FLinearArena::FScopeMarker::FScopeMarker(FScopeMarker&& Other) noexcept
    : ArenaPtr(Other.ArenaPtr)
    , SavedChunk(Other.SavedChunk)
    , SavedCursor(Other.SavedCursor)
    , SavedDestructorNode(Other.SavedDestructorNode)
    , MarkerId(Other.MarkerId) {
    Other.ArenaPtr = nullptr;
    Other.SavedChunk = nullptr;
    Other.SavedCursor = 0;
    Other.SavedDestructorNode = nullptr;
    Other.MarkerId = 0;
}

/**
 * @brief 移动赋值操作符
 * @param Other 被移动标记
 * @return 自身引用
 */
FLinearArena::FScopeMarker& FLinearArena::FScopeMarker::operator=(FScopeMarker&& Other) noexcept {
    if (this != &Other) {
        if (ArenaPtr != nullptr) {
            ArenaPtr->PopAndRewindMarker(MarkerId, SavedChunk, SavedCursor, SavedDestructorNode);
        }
        ArenaPtr = Other.ArenaPtr;
        SavedChunk = Other.SavedChunk;
        SavedCursor = Other.SavedCursor;
        SavedDestructorNode = Other.SavedDestructorNode;
        MarkerId = Other.MarkerId;

        Other.ArenaPtr = nullptr;
        Other.SavedChunk = nullptr;
        Other.SavedCursor = 0;
        Other.SavedDestructorNode = nullptr;
        Other.MarkerId = 0;
    }
    return *this;
}
