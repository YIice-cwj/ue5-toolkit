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

/**
 * @brief 基于刮擦内存构建的轻量级临时动态数组
 * @tparam T 元素类型
 */
template <typename T>
class TScratchArray {
public:
    /**
     * @brief 构造函数：初始化指定容量的刮擦数组
     * @param InScratchpad 临时分配器守卫引用
     * @param InInitialCapacity 初始容量
     */
    explicit TScratchArray(FScopedFrameScratchpad& InScratchpad, int32 InInitialCapacity = 16)
        : Scratchpad(InScratchpad)
        , ElementCapacity(FMath::Max(1, InInitialCapacity))
        , ElementCount(0) {
        Elements = static_cast<T*>(Scratchpad.Allocate(sizeof(T) * ElementCapacity, alignof(T)));
        checkf(Elements != nullptr, TEXT("TScratchArray: Failed to allocate initial scratch memory."));
    }

    /**
     * @brief 析构函数：逆序执行已存活元素的析构函数
     */
    ~TScratchArray() {
        Reset();
    }

    TScratchArray(const TScratchArray&) = delete;
    TScratchArray& operator=(const TScratchArray&) = delete;
    TScratchArray(TScratchArray&&) = delete;
    TScratchArray& operator=(TScratchArray&&) = delete;

    /**
     * @brief 预分配指定容量的连续刮擦内存
     * @param InCapacity 期望最小容量
     */
    void Reserve(int32 InCapacity) {
        if (InCapacity > ElementCapacity) {
            EnsureCapacity(InCapacity);
        }
    }

    /**
     * @brief 原地构造并追加元素到数组末尾
     * @tparam ArgsType 构造实参类型包
     * @param Args 构造实参
     * @return 新构造元素的引用
     */
    template <typename... ArgsType>
    T& Emplace(ArgsType&&... Args) {
        EnsureCapacity(ElementCount + 1);
        T* NewItem = new (&Elements[ElementCount]) T(Forward<ArgsType>(Args)...);
        ++ElementCount;
        return *NewItem;
    }

    /**
     * @brief 追加常量左值元素
     * @param Item 待追加元素
     */
    void Add(const T& Item) {
        EnsureCapacity(ElementCount + 1);
        new (&Elements[ElementCount]) T(Item);
        ++ElementCount;
    }

    /**
     * @brief 追加右值移动元素
     * @param Item 待追加右值
     */
    void Add(T&& Item) {
        EnsureCapacity(ElementCount + 1);
        new (&Elements[ElementCount]) T(MoveTemp(Item));
        ++ElementCount;
    }

    /**
     * @brief 弹出末尾元素
     * @return 弹出的元素拷贝或移动结果
     */
    T Pop() {
        check(ElementCount > 0);
        --ElementCount;
        T Result = MoveTemp(Elements[ElementCount]);
        Elements[ElementCount].~T();
        return Result;
    }

    /**
     * @brief 常量下标访问
     * @param Index 索引
     * @return 目标元素常量引用
     */
    [[nodiscard]] const T& operator[](int32 Index) const {
        check(Index >= 0 && Index < ElementCount);
        return Elements[Index];
    }

    /**
     * @brief 非常量下标访问
     * @param Index 索引
     * @return 目标元素引用
     */
    [[nodiscard]] T& operator[](int32 Index) {
        check(Index >= 0 && Index < ElementCount);
        return Elements[Index];
    }

    /**
     * @brief 获取当前已容纳元素数量
     * @return 元素数量
     */
    [[nodiscard]] int32 Num() const noexcept {
        return ElementCount;
    }

    /**
     * @brief 查询数组是否为空
     * @return 为空返回 true
     */
    [[nodiscard]] bool IsEmpty() const noexcept {
        return ElementCount == 0;
    }

    /**
     * @brief 获取当前分配的容量
     * @return 容量上限
     */
    [[nodiscard]] int32 Capacity() const noexcept {
        return ElementCapacity;
    }

    /**
     * @brief 获取底层连续数据指针
     * @return 连续元素首地址
     */
    [[nodiscard]] const T* GetData() const noexcept {
        return Elements;
    }

    /**
     * @brief 获取底层连续数据指针
     * @return 连续元素首地址
     */
    [[nodiscard]] T* GetData() noexcept {
        return Elements;
    }

    /**
     * @brief 清空并析构当前所有元素
     */
    void Reset() {
        for (int32 Index = ElementCount - 1; Index >= 0; --Index) {
            Elements[Index].~T();
        }
        ElementCount = 0;
    }

    /**
     * @brief 转换为虚幻标准 TArrayView
     * @return 连续数组视图
     */
    [[nodiscard]] TArrayView<T> AsView() noexcept {
        return TArrayView<T>(Elements, ElementCount);
    }

    /**
     * @brief 转换为虚幻标准常量 TConstArrayView
     * @return 常量连续数组视图
     */
    [[nodiscard]] TConstArrayView<T> AsView() const noexcept {
        return TConstArrayView<T>(Elements, ElementCount);
    }

    /**
     * @brief 获取可变起始迭代器指针
     * @return 首元素指针
     */
    [[nodiscard]] T* begin() noexcept {
        return Elements;
    }

    /**
     * @brief 获取常量起始迭代器指针
     * @return 首元素常量指针
     */
    [[nodiscard]] const T* begin() const noexcept {
        return Elements;
    }

    /**
     * @brief 获取可变哨兵末尾迭代器指针
     * @return 尾后元素指针
     */
    [[nodiscard]] T* end() noexcept {
        return Elements + ElementCount;
    }

    /**
     * @brief 获取常量哨兵末尾迭代器指针
     * @return 尾后元素常量指针
     */
    [[nodiscard]] const T* end() const noexcept {
        return Elements + ElementCount;
    }

private:
    /**
     * @brief 保证容量满足期望要求
     * @param MinDesiredCapacity 期望最小容量
     */
    void EnsureCapacity(int32 MinDesiredCapacity) {
        checkf(MinDesiredCapacity > 0, TEXT("TScratchArray: MinDesiredCapacity must be greater than 0."));
        if (MinDesiredCapacity <= ElementCapacity) {
            return;
        }

        const int32 DoubledCapacity = (ElementCapacity <= MAX_int32 / 2) ? (ElementCapacity * 2) : MAX_int32;
        const int32 NewCapacity = FMath::Max(DoubledCapacity, MinDesiredCapacity);
        T* NewElements = static_cast<T*>(Scratchpad.Allocate(sizeof(T) * NewCapacity, alignof(T)));
        checkf(NewElements != nullptr, TEXT("TScratchArray: Failed to allocate scratch memory during capacity expansion."));

        if constexpr (std::is_trivially_copyable_v<T>) {
            if (ElementCount > 0) {
                FMemory::Memcpy(NewElements, Elements, sizeof(T) * ElementCount);
            }
        } else {
            for (int32 Index = 0; Index < ElementCount; ++Index) {
                new (&NewElements[Index]) T(MoveTemp(Elements[Index]));
                Elements[Index].~T();
            }
        }

        Elements = NewElements;
        ElementCapacity = NewCapacity;
    }

private:
    FScopedFrameScratchpad& Scratchpad;
    T* Elements = nullptr;
    int32 ElementCapacity = 0;
    int32 ElementCount = 0;
};
