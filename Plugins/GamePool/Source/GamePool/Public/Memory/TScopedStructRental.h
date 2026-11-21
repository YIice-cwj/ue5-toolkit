/**
 * @file TScopedStructRental.h
 * @brief 原生 C++ 结构体内存池作用域 RAII 自动归还租约卫士模板
 */

#pragma once

#include "CoreMinimal.h"

template <typename T, size_t BlocksPerChunk>
class TStructPool;

/**
 * @brief 原生 C++ 结构体池作用域 RAII 自动归还租约卫士
 * @tparam T 结构体或纯 C++ 类类型
 * @tparam BlocksPerChunk 所属结构体池每个 Chunk 的块容量
 */
template <typename T, size_t BlocksPerChunk = 64>
class TScopedStructRental {
public:
    /**
     * @brief 默认构造空租约
     */
    TScopedStructRental() = default;

    /**
     * @brief 构造绑定结构体池与对象实例的租约（若实例为空则自动退化为空租约）
     * @param InPool 所属结构体池引用
     * @param InInstance 借出的对象实例指针
     */
    TScopedStructRental(TStructPool<T, BlocksPerChunk>& InPool, T* InInstance) noexcept
        : Pool(InInstance != nullptr ? &InPool : nullptr)
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
     * @brief 移动构造函数（转移租约所有权并清空源租约）
     * @param Other 被移动租约
     */
    TScopedStructRental(TScopedStructRental&& Other) noexcept
        : Pool(Other.Pool)
        , Instance(Other.Instance) {
        Other.Pool = nullptr;
        Other.Instance = nullptr;
    }

    /**
     * @brief 移动赋值操作符（先归还自身原有实例，再接管源租约）
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
     * @return 实例非空返回 true
     */
    [[nodiscard]] bool IsValid() const noexcept {
        return Instance != nullptr;
    }

    /**
     * @brief 获取对象原生指针
     * @return 实例指针，空租约返回 nullptr
     */
    [[nodiscard]] T* Get() const noexcept {
        return Instance;
    }

    /**
     * @brief 访问底层对象成员
     * @return 实例指针
     */
    [[nodiscard]] T* operator->() const noexcept {
        return Instance;
    }

    /**
     * @brief 解引用操作符（调用前须确保租约有效）
     * @return 实例引用
     */
    [[nodiscard]] T& operator*() const noexcept {
        checkf(Instance != nullptr, TEXT("Attempted to dereference an invalid TScopedStructRental"));
        return *Instance;
    }

    /**
     * @brief 布尔显式转换操作符
     * @return 有效返回 true
     */
    [[nodiscard]] explicit operator bool() const noexcept {
        return IsValid();
    }

    /**
     * @brief 提前显式归还实例回池并清空租约
     */
    void EarlyRelease() noexcept {
        if (Pool != nullptr && Instance != nullptr) {
            Pool->Release(Instance);
            Instance = nullptr;
            Pool = nullptr;
        }
    }

    /**
     * @brief 脱钩放弃自动归还管理权（不触发归还，转由调用方自行调用池的 Release 归还）
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
