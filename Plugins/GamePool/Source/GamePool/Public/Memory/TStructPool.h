/**
 * @file TStructPool.h
 * @brief 基于 GameCore TFixedSizePool 构建的高性能原生 C++ 结构体/对象内存池
 */

#pragma once

#include "CoreMinimal.h"
#include "Memory/TFixedSizePool.h"
#include "Memory/TScopedStructRental.h"

/**
 * @brief 基于 GameCore TFixedSizePool 构建的高性能原生 C++ 结构体/对象内存池（O(1) 原地构造、析构与 RAII 租约）
 *
 * @note 生命周期契约：移动或销毁本对象池之前，所有已借出的 TScopedStructRental 租约与裸指针必须已归还完毕（IsEmpty() 为 true），
 *       否则残留租约持有的池指针将悬空。
 *
 * @tparam T 结构体或纯 C++ 类类型
 * @tparam BlocksPerChunk 扩容时单次申请的块数量
 */
template <typename T, size_t BlocksPerChunk = 64>
class TStructPool {
public:
    TStructPool() = default;
    ~TStructPool() = default;

    TStructPool(const TStructPool&) = delete;
    TStructPool& operator=(const TStructPool&) = delete;

    /**
     * @brief 移动构造函数（调用前须确保源池无未归还的 TScopedStructRental 活跃租约）
     * @param Other 被移动的源结构体池
     */
    TStructPool(TStructPool&& Other) noexcept
        : InternalPool(MoveTemp(Other.InternalPool)) {
    }

    /**
     * @brief 移动赋值操作符（调用前须确保双方池均无悬空 TScopedStructRental 活跃租约）
     * @param Other 被移动的源结构体池
     * @return 自身引用
     */
    TStructPool& operator=(TStructPool&& Other) noexcept {
        if (this != &Other) {
            InternalPool = MoveTemp(Other.InternalPool);
        }
        return *this;
    }

    /**
     * @brief 原地构造并借出一个实例
     * @tparam TArgs 构造实参包
     * @param Args 构造实参
     * @return 构造成功的对象指针，系统内存耗尽返回 nullptr
     */
    template <typename... TArgs>
    [[nodiscard]] T* Acquire(TArgs&&... Args) {
        return InternalPool.New(Forward<TArgs>(Args)...);
    }

    /**
     * @brief 原地构造并借出 RAII 作用域租约卫士
     * @tparam TArgs 构造实参包
     * @param Args 构造实参
     * @return 租约卫士实例
     */
    template <typename... TArgs>
    [[nodiscard]] TScopedStructRental<T, BlocksPerChunk> AcquireScoped(TArgs&&... Args) {
        T* Instance = Acquire(Forward<TArgs>(Args)...);
        return TScopedStructRental<T, BlocksPerChunk>(*this, Instance);
    }

    /**
     * @brief 析构并归还实例回池（允许安全传入 nullptr）
     * @param Instance 待归还的对象指针
     */
    void Release(T* Instance) {
        InternalPool.Free(Instance);
    }

    /**
     * @brief 预先开辟指定数量的 Chunk 内存块以消除热路径首次扩容抖动
     * @param TargetChunkCount 目标 Chunk 总数
     */
    void ReserveChunks(size_t TargetChunkCount) {
        InternalPool.ReserveChunks(TargetChunkCount);
    }

    /**
     * @brief 清空内存池分配的全部底层大块内存（调用前须确保已归还所有非平凡析构活跃实例）
     */
    void Clear() {
        InternalPool.Reset();
    }

    /**
     * @brief 检查当前是否无借出的活跃实例
     * @return 无活跃借出实例返回 true
     */
    [[nodiscard]] bool IsEmpty() const noexcept {
        return InternalPool.IsEmpty();
    }

    /**
     * @brief 获取当前借出的活跃实例总数
     * @return 活跃实例数
     */
    [[nodiscard]] size_t GetAllocatedCount() const noexcept {
        return InternalPool.GetAllocatedCount();
    }

    /**
     * @brief 获取当前可立即复用且无需扩容的空闲块数量
     * @return 空闲块数量
     */
    [[nodiscard]] size_t GetFreeCount() const noexcept {
        const size_t Total = InternalPool.GetTotalCapacity();
        const size_t Allocated = InternalPool.GetAllocatedCount();
        return Total >= Allocated ? (Total - Allocated) : 0;
    }

    /**
     * @brief 获取当前已开辟的 Chunk 大块数量
     * @return Chunk 数量
     */
    [[nodiscard]] size_t GetChunkCount() const noexcept {
        return InternalPool.GetChunkCount();
    }

    /**
     * @brief 获取当前内存池总容量（以对象数为单位）
     * @return 总容量
     */
    [[nodiscard]] size_t GetTotalCapacity() const noexcept {
        return InternalPool.GetTotalCapacity();
    }

private:
    TFixedSizePool<T, BlocksPerChunk> InternalPool;
};
