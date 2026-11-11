/**
 * @file TFixedBlockAllocator.h
 * @brief 固定尺寸微型内存块分配器与类型化对象池（侵入式空闲链表，O(1) 纳秒级分配与零堆碎片）
 */

#pragma once

#include "CoreMinimal.h"
#include <new>
#include <type_traits>

/**
 * @brief 固定尺寸无锁块分配器（单线程内极速 O(1) 申请与释放）
 * @tparam BlockSize 每个内存块的字节大小（至少为指针尺寸）
 * @tparam BlocksPerChunk 每个大块包含的内存块数量（默认 64）
 * @tparam Alignment 内存对齐字节数（默认 8 字节）
 */
template <size_t BlockSize, size_t BlocksPerChunk = 64, size_t Alignment = alignof(void*)>
class TFixedBlockAllocator {
    static_assert(BlockSize > 0, "BlockSize 必须大于 0");
    static_assert(BlocksPerChunk > 0, "BlocksPerChunk 必须大于 0");
    static_assert((Alignment & (Alignment - 1)) == 0, "Alignment 必须为 2 的幂");

    struct FNode {
        FNode* Next = nullptr;
    };

    static constexpr size_t EffectiveAlignment = (Alignment < alignof(FNode)) ? alignof(FNode) : Alignment;
    static constexpr size_t ActualBlockSize = (BlockSize < sizeof(FNode)) ? sizeof(FNode) : BlockSize;
    static constexpr size_t AlignedBlockSize = (ActualBlockSize + (EffectiveAlignment - 1)) & ~(EffectiveAlignment - 1);

    struct FChunk {
        FChunk* NextChunk = nullptr;
        alignas(EffectiveAlignment) uint8 Data[AlignedBlockSize * BlocksPerChunk];
    };

public:
    TFixedBlockAllocator() = default;

    ~TFixedBlockAllocator() {
        Reset();
    }

    TFixedBlockAllocator(const TFixedBlockAllocator&) = delete;
    TFixedBlockAllocator& operator=(const TFixedBlockAllocator&) = delete;

    TFixedBlockAllocator(TFixedBlockAllocator&& Other) noexcept
        : FreeList(Other.FreeList)
        , ChunkList(Other.ChunkList)
        , AllocatedCount(Other.AllocatedCount)
        , ChunkCount(Other.ChunkCount) {
        Other.FreeList = nullptr;
        Other.ChunkList = nullptr;
        Other.AllocatedCount = 0;
        Other.ChunkCount = 0;
    }

    TFixedBlockAllocator& operator=(TFixedBlockAllocator&& Other) noexcept {
        if (this != &Other) {
            Reset();
            FreeList = Other.FreeList;
            ChunkList = Other.ChunkList;
            AllocatedCount = Other.AllocatedCount;
            ChunkCount = Other.ChunkCount;

            Other.FreeList = nullptr;
            Other.ChunkList = nullptr;
            Other.AllocatedCount = 0;
            Other.ChunkCount = 0;
        }
        return *this;
    }

    /**
     * @brief 分配一个固定尺寸的内存块
     * @return 分配成功的内存首地址，若系统内存耗尽返回 nullptr
     */
    void* Allocate() {
        if (FreeList == nullptr) {
            if (!AllocateNewChunk()) {
                return nullptr;
            }
        }

        FNode* Node = FreeList;
        FreeList = FreeList->Next;
        ++AllocatedCount;
        return static_cast<void*>(Node);
    }

    /**
     * @brief 归还一个先前分配的内存块到空闲链表
     * @param Ptr 待归还的内存地址（允许传入 nullptr）
     */
    void Deallocate(void* Ptr) {
        if (Ptr == nullptr) {
            return;
        }

        checkf(AllocatedCount > 0, TEXT("TFixedBlockAllocator: Deallocate called when AllocatedCount is 0 (potential double-free)."));

        FNode* Node = static_cast<FNode*>(Ptr);
        Node->Next = FreeList;
        FreeList = Node;
        if (AllocatedCount > 0) {
            --AllocatedCount;
        }
    }

    /**
     * @brief 释放所有大块内存并重置分配器状态
     */
    void Reset() {
        FChunk* Current = ChunkList;
        while (Current != nullptr) {
            FChunk* Next = Current->NextChunk;
            FMemory::Free(Current);
            Current = Next;
        }

        ChunkList = nullptr;
        FreeList = nullptr;
        AllocatedCount = 0;
        ChunkCount = 0;
    }

    /**
     * @brief 查询当前已被借出的活动内存块数量
     * @return 活动块总数
     */
    [[nodiscard]] size_t GetAllocatedCount() const noexcept {
        return AllocatedCount;
    }

    /**
     * @brief 查询当前分配器已开辟的 Chunk 大块数量
     * @return Chunk 总数
     */
    [[nodiscard]] size_t GetChunkCount() const noexcept {
        return ChunkCount;
    }

    /**
     * @brief 查询当前分配器的总容量（以块为单位）
     * @return 容量总块数
     */
    [[nodiscard]] size_t GetTotalCapacity() const noexcept {
        return ChunkCount * BlocksPerChunk;
    }

    /**
     * @brief 查询当前分配器对齐后的单个块尺寸
     * @return 对齐后块字节数
     */
    [[nodiscard]] static constexpr size_t GetAlignedBlockSize() noexcept {
        return AlignedBlockSize;
    }

private:
    /**
     * @brief 开辟并初始化一个新的 Chunk 大块，将其内部的块挂入空闲链表
     * @return 成功开辟返回 true
     */
    bool AllocateNewChunk() {
        void* RawMemory = FMemory::Malloc(sizeof(FChunk), static_cast<uint32>(EffectiveAlignment));
        if (RawMemory == nullptr) {
            return false;
        }

        FChunk* NewChunk = static_cast<FChunk*>(RawMemory);
        NewChunk->NextChunk = ChunkList;
        ChunkList = NewChunk;
        ++ChunkCount;

        for (size_t Index = 0; Index < BlocksPerChunk; ++Index) {
            uint8* BlockPtr = &NewChunk->Data[Index * AlignedBlockSize];
            FNode* Node = reinterpret_cast<FNode*>(BlockPtr);
            Node->Next = FreeList;
            FreeList = Node;
        }

        return true;
    }

private:
    FNode* FreeList = nullptr;
    FChunk* ChunkList = nullptr;
    size_t AllocatedCount = 0;
    size_t ChunkCount = 0;
};

/**
 * @brief 基于固定尺寸内存块的类型化对象池（纳秒级原地构造与析构管理）
 * @tparam T 对象类型
 * @tparam BlocksPerChunk 每次扩容时的大块对象容量
 */
template <typename T, size_t BlocksPerChunk = 64>
class TFixedSizePool {
public:
    TFixedSizePool() = default;
    ~TFixedSizePool() = default;

    TFixedSizePool(const TFixedSizePool&) = delete;
    TFixedSizePool& operator=(const TFixedSizePool&) = delete;
    TFixedSizePool(TFixedSizePool&&) noexcept = default;
    TFixedSizePool& operator=(TFixedSizePool&&) noexcept = default;

    /**
     * @brief 原地构造并获取一个对象实例
     * @tparam ArgsType 构造实参类型包
     * @param Args 构造实参
     * @return 构造成功的对象指针，若内存耗尽返回 nullptr
     */
    template <typename... ArgsType>
    [[nodiscard]] T* New(ArgsType&&... Args) {
        void* Memory = BlockAllocator.Allocate();
        if (Memory == nullptr) {
            return nullptr;
        }
        return ::new (Memory) T(Forward<ArgsType>(Args)...);
    }

    /**
     * @brief 析构并归还一个对象实例到对象池
     * @param ObjectPtr 待归还的对象指针
     */
    void Free(T* ObjectPtr) {
        if (ObjectPtr != nullptr) {
            ObjectPtr->~T();
            BlockAllocator.Deallocate(ObjectPtr);
        }
    }

    /**
     * @brief 清空对象池底层分配的所有 Chunk 内存
     */
    void Reset() {
        BlockAllocator.Reset();
    }

    /**
     * @brief 获取当前借出的活跃对象数量
     * @return 活跃对象数量
     */
    [[nodiscard]] size_t GetAllocatedCount() const noexcept {
        return BlockAllocator.GetAllocatedCount();
    }

    /**
     * @brief 获取当前已开辟的 Chunk 数量
     * @return Chunk 总数
     */
    [[nodiscard]] size_t GetChunkCount() const noexcept {
        return BlockAllocator.GetChunkCount();
    }

    /**
     * @brief 获取当前对象池的总容量
     * @return 总容量对象数
     */
    [[nodiscard]] size_t GetTotalCapacity() const noexcept {
        return BlockAllocator.GetTotalCapacity();
    }

private:
    TFixedBlockAllocator<sizeof(T), BlocksPerChunk, alignof(T)> BlockAllocator;
};
