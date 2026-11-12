// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include <type_traits>

class UObjectPoolSubsystem;

/**
 * @brief 带世代版本防悬空检查的池化对象受管安全句柄
 * @tparam T 对象具体类型（AActor 派生类或 UObject 派生类）
 */
template <typename T>
class TPooledObjectHandle {
public:
    /**
     * @brief 默认构造空句柄
     */
    TPooledObjectHandle() = default;

    /**
     * @brief 构造受管句柄实例
     * @param InObject 目标对象指针
     * @param InSubsystem 所属对象池子系统指针
     * @param InGeneration 借出世代版本序号
     */
    TPooledObjectHandle(T* InObject, UObjectPoolSubsystem* InSubsystem, uint32 InGeneration)
        : ObjectPtr(InObject)
        , SubsystemPtr(InSubsystem)
        , Generation(InGeneration) {
    }

    /**
     * @brief 校验句柄是否依然指向当前借出的有效代数
     * @return 对象存在、子系统有效且世代匹配返回 true
     */
    [[nodiscard]] bool IsValid() const;

    /**
     * @brief 安全获取对象指针
     * @return 世代匹配且未归还时返回对象指针，已失效返回 nullptr
     */
    [[nodiscard]] T* Get() const {
        return IsValid() ? ObjectPtr.Get() : nullptr;
    }

    /**
     * @brief 访问底层对象成员
     * @return 对象原生指针
     */
    T* operator->() const {
        return Get();
    }

    /**
     * @brief 布尔转换操作符
     * @return 句柄有效返回 true
     */
    explicit operator bool() const {
        return IsValid();
    }

    /**
     * @brief 获取句柄记录的借出世代序号
     * @return 世代版本号
     */
    [[nodiscard]] uint32 GetGeneration() const noexcept {
        return Generation;
    }

    /**
     * @brief 获取关联的对象池子系统指针
     * @return 子系统指针，已销毁返回 nullptr
     */
    [[nodiscard]] UObjectPoolSubsystem* GetSubsystem() const noexcept {
        return SubsystemPtr.Get();
    }

    /**
     * @brief 句柄相等性比较
     * @param Other 对比句柄
     * @return 对象、子系统与世代一致返回 true
     */
    bool operator==(const TPooledObjectHandle& Other) const noexcept {
        return ObjectPtr == Other.ObjectPtr &&
               SubsystemPtr == Other.SubsystemPtr &&
               Generation == Other.Generation;
    }

    /**
     * @brief 句柄不等性比较
     * @param Other 对比句柄
     * @return 存在差异返回 true
     */
    bool operator!=(const TPooledObjectHandle& Other) const noexcept {
        return !(*this == Other);
    }

    /**
     * @brief 重置句柄为初始空状态
     */
    void Reset() noexcept {
        ObjectPtr.Reset();
        SubsystemPtr.Reset();
        Generation = 0;
    }

private:
    TWeakObjectPtr<T> ObjectPtr;
    TWeakObjectPtr<UObjectPoolSubsystem> SubsystemPtr;
    uint32 Generation = 0;
};

/**
 * @brief 作用域 RAII 自动归还借出卫士（离开作用域自动安全归还入池）
 * @tparam T 对象具体类型
 */
template <typename T>
class TScopedPoolRental {
public:
    /**
     * @brief 默认构造空租约
     */
    TScopedPoolRental() = default;

    /**
     * @brief 从受管安全句柄构造租约卫士
     * @param InHandle 安全句柄
     */
    explicit TScopedPoolRental(TPooledObjectHandle<T> InHandle)
        : Handle(MoveTemp(InHandle)) {
    }

    /**
     * @brief 析构时若租约有效则自动触发归还回池
     */
    ~TScopedPoolRental() {
        EarlyRelease();
    }

    TScopedPoolRental(const TScopedPoolRental&) = delete;
    TScopedPoolRental& operator=(const TScopedPoolRental&) = delete;

    /**
     * @brief 移动构造函数
     * @param Other 被移动租约
     */
    TScopedPoolRental(TScopedPoolRental&& Other) noexcept
        : Handle(MoveTemp(Other.Handle)) {
        Other.Handle.Reset();
    }

    /**
     * @brief 移动赋值操作符
     * @param Other 被移动租约
     * @return 自身引用
     */
    TScopedPoolRental& operator=(TScopedPoolRental&& Other) noexcept {
        if (this != &Other) {
            EarlyRelease();
            Handle = MoveTemp(Other.Handle);
            Other.Handle.Reset();
        }
        return *this;
    }

    /**
     * @brief 校验当前租约是否有效
     * @return 句柄有效返回 true
     */
    [[nodiscard]] bool IsValid() const {
        return Handle.IsValid();
    }

    /**
     * @brief 获取借出对象指针
     * @return 有效指针或 nullptr
     */
    [[nodiscard]] T* Get() const {
        return Handle.Get();
    }

    /**
     * @brief 访问底层对象成员
     * @return 对象原生指针
     */
    T* operator->() const {
        return Get();
    }

    /**
     * @brief 布尔类型转换
     * @return 租约有效返回 true
     */
    explicit operator bool() const {
        return IsValid();
    }

    /**
     * @brief 获取底层持有的安全句柄常量引用
     * @return 句柄引用
     */
    [[nodiscard]] const TPooledObjectHandle<T>& GetHandle() const noexcept {
        return Handle;
    }

    /**
     * @brief 提前显式归还对象入池并重置租约
     */
    void EarlyRelease();

    /**
     * @brief 脱钩放弃池管理所有权（不触发归还，转由调用方负责管理）
     * @return 对象指针
     */
    T* ReleaseAndDetach() noexcept {
        T* Raw = Handle.Get();
        Handle.Reset();
        return Raw;
    }

private:
    TPooledObjectHandle<T> Handle;
};
