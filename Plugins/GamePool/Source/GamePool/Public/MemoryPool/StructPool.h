// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Memory/TFixedSizePool.h"
#include <type_traits>

template <typename T, size_t BlocksPerChunk>
class TStructPool;

/**
 * @brief 原生 C++ 结构体池作用域 RAII 自动归还租约卫士
 * @tparam T 结构体或纯 C++ 类类型
 * @tparam BlocksPerChunk 结构体池每个 Chunk 块容量
 */
template <typename T, size_t BlocksPerChunk = 64>
class TScopedStructRental {
public:
    /**
     * @brief 默认构造空租约
     */
    TScopedStructRental() = default;

    /**
     * @brief 构造绑定结构体池与对象实例的租约
     * @param InPool 所属结构体池引用
     * @param InInstance 借出的对象实例指针
     */
    TScopedStructRental(TStructPool<T, BlocksPerChunk>& InPool, T* InInstance) noexcept
        : Pool(&InPool)
        , Instance(InInstance) {
    }

    /**
     * @brief 析构时若持有有效实例则自动归还回池
     */
    ~TScopedStructRental() {
        EarlyRelease();
    }

    TScopedStructRental(const TScopedStructRental&) = delete;
    TScopedStructRental& operator=(const TScopedStructRental&) = delete;

    /**
     * @brief 移动构造函数
     * @param Other 被移动租约
     */
    TScopedStructRental(TScopedStructRental&& Other) noexcept
        : Pool(Other.Pool)
        , Instance(Other.Instance) {
        Other.Pool = nullptr;
        Other.Instance = nullptr;
    }

    /**
     * @brief 移动赋值操作符
     * @param Other 被移动租约
     * @return 自身引用
     */
    TScopedStructRental& operator=(TScopedStructRental&& Other) noexcept {
        if (this != &Other) {
            EarlyRelease();
            Pool = Other.Pool;
            Instance = Other.Instance;
            Other.Pool = nullptr;
            Other.Instance = nullptr;
        }
        return *this;
    }

    /**
     * @brief 校验租约是否持有有效实例
     * @return 实例有效返回 true
     */
    [[nodiscard]] bool IsValid() const noexcept {
        return Instance != nullptr;
    }

    /**
     * @brief 获取对象原生指针
     * @return 实例指针
     */
    [[nodiscard]] T* Get() const noexcept {
        return Instance;
    }

    /**
     * @brief 访问底层对象成员
     * @return 实例指针
     */
    T* operator->() const noexcept {
        return Instance;
    }

    /**
     * @brief 解引用操作符
     * @return 实例引用
     */
    T& operator*() const noexcept {
        check(Instance != nullptr);
        return *Instance;
    }

    /**
     * @brief 布尔类型转换
     * @return 有效返回 true
     */
    explicit operator bool() const noexcept {
        return IsValid();
    }

    /**
     * @brief 提前显式归还实例回池
     */
    void EarlyRelease() noexcept {
        if (Pool != nullptr && Instance != nullptr) {
            Pool->Release(Instance);
            Instance = nullptr;
            Pool = nullptr;
        }
    }

    /**
     * @brief 脱钩放弃池管理所有权（不触发归还，转由调用方负责生命周期）
     * @return 实例指针
     */
    [[nodiscard]] T* ReleaseAndDetach() noexcept {
        T* Out = Instance;
        Instance = nullptr;
        Pool = nullptr;
        return Out;
    }

private:
    TStructPool<T, BlocksPerChunk>* Pool = nullptr;
    T* Instance = nullptr;
};

/**
 * @brief 基于 GameCore TFixedBlockAllocator 构建的高性能原生 C++ 结构体/对象内存池
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
    TStructPool(TStructPool&&) noexcept = default;
    TStructPool& operator=(TStructPool&&) noexcept = default;

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
     * @brief 析构并归还实例回池
     * @param Instance 待归还的对象指针
     */
    void Release(T* Instance) {
        InternalPool.Free(Instance);
    }

    /**
     * @brief 清空内存池分配的全部底极大块
     */
    void Clear() {
        InternalPool.Reset();
    }

    /**
     * @brief 获取当前借出的活跃实例总数
     * @return 活跃实例数
     */
    [[nodiscard]] size_t GetAllocatedCount() const noexcept {
        return InternalPool.GetAllocatedCount();
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
