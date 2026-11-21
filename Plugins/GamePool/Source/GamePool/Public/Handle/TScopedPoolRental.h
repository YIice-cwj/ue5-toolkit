/**
 * @file TScopedPoolRental.h
 * @brief 池化 Actor / UObject 作用域 RAII 自动归还租约卫士模板
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Handle/TPooledObjectHandle.h"

#include <type_traits>

/**
 * @brief 作用域 RAII 自动归还借出卫士（离开作用域自动按世代安全归还回池，防遗漏泄漏）
 * @tparam T 对象具体类型（必须为 UObject 或 AActor 派生类）
 */
template <typename T>
class TScopedPoolRental {
    static_assert(std::is_base_of_v<UObject, T>, "TScopedPoolRental<T> 仅支持 UObject 派生类型");

public:
    /**
     * @brief 默认构造空租约
     */
    TScopedPoolRental() = default;

    /**
     * @brief 从受管安全句柄构造租约卫士
     * @param InHandle 安全句柄
     */
    explicit TScopedPoolRental(TPooledObjectHandle<T> InHandle) noexcept
        : Handle(MoveTemp(InHandle)) {
    }

    /**
     * @brief 析构时若租约仍有效则自动触发归还回池
     */
    ~TScopedPoolRental() {
        EarlyRelease();
    }

    TScopedPoolRental(const TScopedPoolRental&) = delete;
    TScopedPoolRental& operator=(const TScopedPoolRental&) = delete;

    /**
     * @brief 移动构造函数（转移租约所有权并清空源租约）
     * @param Other 被移动租约
     */
    TScopedPoolRental(TScopedPoolRental&& Other) noexcept
        : Handle(MoveTemp(Other.Handle)) {
        Other.Handle.Reset();
    }

    /**
     * @brief 移动赋值操作符（先归还自身原有租约，再接管源租约）
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
     * @return 句柄仍处于当前有效借出世代返回 true
     */
    [[nodiscard]] bool IsValid() const {
        return Handle.IsValid();
    }

    /**
     * @brief 获取借出对象指针
     * @return 有效对象指针，已归还或失效返回 nullptr
     */
    [[nodiscard]] T* Get() const {
        return Handle.Get();
    }

    /**
     * @brief 访问底层对象成员
     * @return 有效对象指针，失效返回 nullptr
     */
    [[nodiscard]] T* operator->() const {
        return Get();
    }

    /**
     * @brief 解引用底层对象（调用前须确保租约处于有效状态）
     * @return 对象引用
     */
    [[nodiscard]] T& operator*() const {
        return *Handle;
    }

    /**
     * @brief 布尔显式转换操作符
     * @return 租约有效返回 true
     */
    [[nodiscard]] explicit operator bool() const {
        return IsValid();
    }

    /**
     * @brief 获取底层持有的安全句柄常量引用
     * @return 句柄常量引用
     */
    [[nodiscard]] const TPooledObjectHandle<T>& GetHandle() const noexcept {
        return Handle;
    }

    /**
     * @brief 提前显式归还对象入池并重置租约（单次世代校验，零重复查表）
     */
    void EarlyRelease() noexcept {
        T* Raw = Handle.Get();
        UObjectPoolSubsystem* Subsystem = Handle.GetSubsystem();
        Handle.Reset();
        if (Raw == nullptr || Subsystem == nullptr) {
            return;
        }
        if constexpr (std::is_base_of_v<AActor, T>) {
            Game::Pool::Private::ReleasePooledActor(Subsystem, Raw);
        } else {
            Game::Pool::Private::ReleasePooledObject(Subsystem, Raw);
        }
    }

    /**
     * @brief 脱钩放弃自动归还管理权（不触发归还，转由调用方自行负责归还）
     * @return 有效对象指针，已失效返回 nullptr
     */
    [[nodiscard]] T* ReleaseAndDetach() noexcept {
        T* Raw = Handle.Get();
        Handle.Reset();
        return Raw;
    }

private:
    TPooledObjectHandle<T> Handle;
};
