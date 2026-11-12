// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Memory/TFixedBlockAllocator.h"
#include <new>
#include <type_traits>

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
     * @brief 获取当前借出的活跃对象数量（等价于 GetAllocatedCount）
     * @return 活跃对象数量
     */
    [[nodiscard]] int32 Num() const noexcept {
        return static_cast<int32>(GetAllocatedCount());
    }

    /**
     * @brief 检查当前是否有借出的活跃对象
     * @return 无活跃借出对象返回 true
     */
    [[nodiscard]] bool IsEmpty() const noexcept {
        return GetAllocatedCount() == 0;
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
