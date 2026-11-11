/**
 * @file FGameServiceLocator.h
 * @brief 纯 C++ 线程安全的轻量服务定位与依赖注入容器（零 RTTI，零 UObject 依赖）
 */

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeRWLock.h"
#include "Templates/SharedPointer.h"
#include "Listener/FGameTypeFingerprint.h"

/**
 * @brief 纯 C++ 线程安全服务定位器
 *
 * 专为脱离 UWorld/UGameInstance 约束的纯逻辑架构与多线程子系统设计：
 * 1. 纯接口依赖倒置，解耦模块间物理依赖；
 * 2. 基于 64 位无 RTTI 编译期类型指纹，消除虚函数反射与 RTTI 依赖；
 * 3. 支持类型隐式解析与显式 FName 命名服务双重寻址；
 * 4. 读写分离锁（FRWLock）保护高频并发只读解析。
 */
class GAMECORE_API FGameServiceLocator {
public:
    FGameServiceLocator() = default;
    ~FGameServiceLocator() = default;

    FGameServiceLocator(const FGameServiceLocator&) = delete;
    FGameServiceLocator& operator=(const FGameServiceLocator&) = delete;

    /**
     * @brief 获取全局单例服务定位器
     * @return 引用实例
     */
    static FGameServiceLocator& Get();

    /**
     * @brief 注册基于抽象接口类型或指定服务名的单例服务
     * @tparam TInterface 抽象接口类型
     * @param ServiceInstance 服务实现实例的线程安全共享引用
     * @param ServiceName 可选具名服务标识（为 NAME_None 时注册为类型单例服务）
     */
    template <typename TInterface>
    void RegisterService(
        TSharedRef<TInterface, ESPMode::ThreadSafe> ServiceInstance,
        FName ServiceName = NAME_None
    ) {
        TSharedPtr<TInterface, ESPMode::ThreadSafe> TypedPtr = ServiceInstance;
        TSharedPtr<void, ESPMode::ThreadSafe> VoidPtr = StaticCastSharedPtr<void>(TypedPtr);
        FWriteScopeLock WriteLock(Lock);
        if (ServiceName.IsNone()) {
            const uint64 ServiceId = Game::Core::GetTypeFingerprint<TInterface>();
            TypeServices.Add(ServiceId, MoveTemp(VoidPtr));
        } else {
            NamedServices.Add(ServiceName, MoveTemp(VoidPtr));
        }
    }

    /**
     * @brief 获取已注册的类型单例或具名服务
     * @tparam TInterface 抽象接口类型
     * @param ServiceName 可选具名服务标识（为 NAME_None 时按类型解析）
     * @return 存在返回共享指针，未注册返回 nullptr
     */
    template <typename TInterface>
    [[nodiscard]] TSharedPtr<TInterface, ESPMode::ThreadSafe> Resolve(FName ServiceName = NAME_None) const {
        FReadScopeLock ReadLock(Lock);
        if (ServiceName.IsNone()) {
            const uint64 ServiceId = Game::Core::GetTypeFingerprint<TInterface>();
            if (const auto* Found = TypeServices.Find(ServiceId)) {
                return StaticCastSharedPtr<TInterface>(*Found);
            }
        } else {
            if (const auto* Found = NamedServices.Find(ServiceName)) {
                return StaticCastSharedPtr<TInterface>(*Found);
            }
        }
        return nullptr;
    }

    /**
     * @brief 查询类型单例服务是否已注册
     * @tparam TInterface 抽象接口类型
     * @return 已注册返回 true
     */
    template <typename TInterface>
    [[nodiscard]] bool HasService() const {
        const uint64 ServiceId = Game::Core::GetTypeFingerprint<TInterface>();
        FReadScopeLock ReadLock(Lock);
        return TypeServices.Contains(ServiceId);
    }

    /**
     * @brief 查询具名服务是否已注册
     * @param ServiceName 具名服务标识
     * @return 已注册返回 true
     */
    [[nodiscard]] bool HasService(FName ServiceName) const {
        FReadScopeLock ReadLock(Lock);
        return NamedServices.Contains(ServiceName);
    }

    /**
     * @brief 注销类型单例服务
     * @tparam TInterface 抽象接口类型
     * @return 成功移除返回 true，未找到返回 false
     */
    template <typename TInterface>
    bool UnregisterService() {
        const uint64 ServiceId = Game::Core::GetTypeFingerprint<TInterface>();
        FWriteScopeLock WriteLock(Lock);
        return TypeServices.Remove(ServiceId) > 0;
    }

    /**
     * @brief 注销具名服务
     * @param ServiceName 具名服务标识
     * @return 成功移除返回 true，未找到返回 false
     */
    bool UnregisterService(FName ServiceName) {
        FWriteScopeLock WriteLock(Lock);
        return NamedServices.Remove(ServiceName) > 0;
    }

    /**
     * @brief 清空重置所有服务（供会话结束或单元测试清理）
     */
    void Reset();

    /**
     * @brief 获取当前注册的服务总数（类型服务 + 具名服务）
     * @return 服务数量
     */
    [[nodiscard]] int32 Num() const;

private:
    mutable FRWLock Lock;
    TMap<uint64, TSharedPtr<void, ESPMode::ThreadSafe>> TypeServices;
    TMap<FName, TSharedPtr<void, ESPMode::ThreadSafe>> NamedServices;
};
