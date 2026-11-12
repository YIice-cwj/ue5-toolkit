/**
 * @file FScopedFrameScratchpad.h
 * @brief 作用域级单帧刮擦内存分配器与连续动态临时数组
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Memory/TLinearArena.h"

#include <new>
#include <type_traits>

/**
 * @brief 作用域级单帧临时内存分配器（RAII 守卫）
 *
 * 在当前作用域内分配极速临时内存，离开作用域时通过 FScopeMarker
 * 自动回退游标并执行非平凡对象析构，零 Malloc、零锁争用、零内存碎片。
 */
class GAMECORE_API FScopedFrameScratchpad {
public:
    /**
     * @brief 获取当前线程专属的单帧线性分配器单例
     * @return 线程局部分配器引用
     */
    static FLinearArena& GetThreadLocalArena();

    /**
     * @brief 强制重置当前线程的分配器（在每帧结束时调用）
     */
    static void ResetThreadLocalArena();

public:
    /**
     * @brief 构造函数：默认绑定当前线程的单帧内存池并设置 RAII 回退点
     */
    explicit FScopedFrameScratchpad();

    /**
     * @brief 构造函数：指定自定义分配器并设置 RAII 回退点
     * @param InArena 自定义分配器引用
     */
    explicit FScopedFrameScratchpad(FLinearArena& InArena);

    /**
     * @brief 析构函数：自动触发作用域回退
     */
    ~FScopedFrameScratchpad() = default;

    FScopedFrameScratchpad(const FScopedFrameScratchpad&) = delete;
    FScopedFrameScratchpad& operator=(const FScopedFrameScratchpad&) = delete;
    FScopedFrameScratchpad(FScopedFrameScratchpad&&) = delete;
    FScopedFrameScratchpad& operator=(FScopedFrameScratchpad&&) = delete;

    /**
     * @brief 分配指定大小与对齐的原生连续内存
     * @param InSize 期望字节数
     * @param InAlignment 内存对齐字节数
     * @return 内存块首地址指针
     */
    [[nodiscard]] void* Allocate(size_t InSize, size_t InAlignment = alignof(std::max_align_t));

    /**
     * @brief 在刮擦内存中构造对象实例
     * @tparam T 目标对象类型
     * @tparam TArgs 构造参数包
     * @param Args 传递给构造函数的参数
     * @return 构造完成的对象指针
     */
    template <typename T, typename... TArgs>
    [[nodiscard]] T* New(TArgs&&... Args) {
        return ArenaRef.New<T>(Forward<TArgs>(Args)...);
    }

    /**
     * @brief 分配未初始化的连续元素数组内存
     * @tparam T 元素类型
     * @param Count 元素数量
     * @return 元素数组视图
     */
    template <typename T>
    [[nodiscard]] TArrayView<T> AllocArray(int32 Count) {
        if (Count <= 0) {
            return TArrayView<T>();
        }
        T* Data = ArenaRef.AllocateArray<T>(static_cast<size_t>(Count));
        if (Data == nullptr) {
            return TArrayView<T>();
        }
        return TArrayView<T>(Data, Count);
    }

    /**
     * @brief 分配并构造连续元素数组（非平凡对象自动注册作用域逆序析构）
     * @tparam T 元素类型
     * @tparam Args 元素构造实参类型包
     * @param Count 元素数量
     * @param InArgs 传递给每个元素的构造参数
     * @return 元素数组视图
     */
    template <typename T, typename... Args>
    [[nodiscard]] TArrayView<T> NewArray(int32 Count, Args&&... InArgs) {
        if (Count <= 0) {
            return TArrayView<T>();
        }
        T* Data = ArenaRef.NewArray<T>(static_cast<size_t>(Count), Forward<Args>(InArgs)...);
        if (Data == nullptr) {
            return TArrayView<T>();
        }
        return TArrayView<T>(Data, Count);
    }

    /**
     * @brief 获取底层绑定的分配器引用
     * @return 分配器引用
     */
    [[nodiscard]] FLinearArena& GetArena() noexcept {
        return ArenaRef;
    }

private:
    FLinearArena& ArenaRef;
    FLinearArena::FScopeMarker ScopeMarker;
};
