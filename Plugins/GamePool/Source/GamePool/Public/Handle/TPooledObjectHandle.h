/**
 * @file TPooledObjectHandle.h
 * @brief 带世代版本防悬空校验的池化对象安全受管句柄模板
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include <type_traits>

class AActor;
class UObjectPoolSubsystem;

namespace Game::Pool::Private {

/**
 * @brief 校验受管句柄的世代版本是否与对象池子系统记录一致（直接复用 FWeakObjectPtr 避免反查 GUObjectArray）
 * @param Subsystem 对象池子系统指针
 * @param ObjectWeak 目标对象弱引用
 * @param Generation 句柄持有的世代序号
 * @return 世代匹配且有效返回 true
 */
GAMEPOOL_API bool ValidatePooledHandle(
    const UObjectPoolSubsystem* Subsystem,
    const FWeakObjectPtr& ObjectWeak,
    uint32 Generation
);

/**
 * @brief 将受管 Actor 归还给对象池子系统
 * @param Subsystem 对象池子系统指针
 * @param Actor 待归还 Actor
 */
GAMEPOOL_API void ReleasePooledActor(UObjectPoolSubsystem* Subsystem, AActor* Actor);

/**
 * @brief 将受管 UObject 归还给对象池子系统
 * @param Subsystem 对象池子系统指针
 * @param Object 待归还 UObject
 */
GAMEPOOL_API void ReleasePooledObject(UObjectPoolSubsystem* Subsystem, UObject* Object);

}  // namespace Game::Pool::Private

/**
 * @brief 带世代版本防悬空与防 Use-After-Return 窜号校验的池化对象安全句柄
 * @tparam T 对象具体类型（必须为 UObject 或 AActor 派生类）
 */
template <typename T>
class TPooledObjectHandle {
    static_assert(std::is_base_of_v<UObject, T>, "TPooledObjectHandle<T> 仅支持 UObject 派生类型");

public:
    /**
     * @brief 默认构造空句柄
     */
    TPooledObjectHandle() = default;

    /**
     * @brief 构造受管安全句柄实例
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
     * @brief 校验句柄是否依然指向当前有效借出世代
     * @return 对象存活、子系统有效且世代匹配返回 true
     */
    [[nodiscard]] bool IsValid() const {
        if (Generation == 0u || !ObjectPtr.IsValid()) {
            return false;
        }
        const UObjectPoolSubsystem* Subsystem = SubsystemPtr.Get();
        if (Subsystem == nullptr) {
            return false;
        }
        return Game::Pool::Private::ValidatePooledHandle(Subsystem, ObjectPtr, Generation);
    }

    /**
     * @brief 安全获取底层对象指针（单次校验提取，已归还或复用窜号时返回 nullptr）
     * @return 有效借出态对象指针，失效返回 nullptr
     */
    [[nodiscard]] T* Get() const {
        if (Generation == 0u) {
            return nullptr;
        }
        UObject* RawObject = ObjectPtr.Get();
        const UObjectPoolSubsystem* Subsystem = SubsystemPtr.Get();
        if (RawObject == nullptr || Subsystem == nullptr) {
            return nullptr;
        }
        return Game::Pool::Private::ValidatePooledHandle(Subsystem, ObjectPtr, Generation)
            ? static_cast<T*>(RawObject)
            : nullptr;
    }

    /**
     * @brief 成员指针访问运算符
     * @return 有效对象指针，失效返回 nullptr
     */
    [[nodiscard]] T* operator->() const {
        return Get();
    }

    /**
     * @brief 解引用运算符（调用前须确保句柄处于有效状态）
     * @return 底层对象引用
     */
    [[nodiscard]] T& operator*() const {
        T* Raw = Get();
        checkf(Raw != nullptr, TEXT("Attempted to dereference an invalid TPooledObjectHandle"));
        return *Raw;
    }

    /**
     * @brief 布尔显式转换操作符
     * @return 句柄有效返回 true
     */
    [[nodiscard]] explicit operator bool() const {
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
     * @brief 句柄相等性比较（C++20 自动合成 operator!=）
     * @param Other 对比句柄
     * @return 对象、子系统与世代均一致返回 true
     */
    [[nodiscard]] bool operator==(const TPooledObjectHandle& Other) const noexcept {
        return ObjectPtr == Other.ObjectPtr &&
               SubsystemPtr == Other.SubsystemPtr &&
               Generation == Other.Generation;
    }

    /**
     * @brief 友元哈希计算函数，支持在 TMap / TSet 中作为键
     * @param Handle 待计算句柄
     * @return 组合哈希值
     */
    friend uint32 GetTypeHash(const TPooledObjectHandle& Handle) noexcept {
        return HashCombine(GetTypeHash(Handle.ObjectPtr), HashCombine(GetTypeHash(Handle.SubsystemPtr), Handle.Generation));
    }

    /**
     * @brief 重置句柄为空状态
     */
    void Reset() noexcept {
        ObjectPtr.Reset();
        SubsystemPtr.Reset();
        Generation = 0u;
    }

private:
    FWeakObjectPtr ObjectPtr;
    TWeakObjectPtr<UObjectPoolSubsystem> SubsystemPtr;
    uint32 Generation = 0u;
};
