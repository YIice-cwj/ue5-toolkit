/**
 * @file FPoolResilienceController.h
 * @brief 内存弹性伸缩与低内存应急泄洪内部容灾控制器
 */

#pragma once

#include "CoreMinimal.h"
#include "Misc/CoreDelegates.h"
#include "UObject/WeakObjectPtr.h"

class FActorPoolManager;
class FUObjectPoolManager;
class UObjectPoolSubsystem;

/**
 * @brief 内存弹性伸缩与容灾防御控制器（安全监听系统低内存修剪广播，支持重复初始化防护与非主线程安全切回）
 */
class FPoolResilienceController {
public:
    FPoolResilienceController() = default;
    ~FPoolResilienceController() {
        Deinitialize();
    }

    FPoolResilienceController(const FPoolResilienceController&) = delete;
    FPoolResilienceController& operator=(const FPoolResilienceController&) = delete;

    /**
     * @brief 初始化弹性控制器并注册引擎内存修剪委托（重复调用前自动清理旧委托防泄漏）
     * @param InOwnerSubsystem 所属子系统对象指针（用于跨线程回调时的弱宿主存活守卫）
     * @param InActorManager Actor 池管理器
     * @param InObjectManager UObject 池管理器
     */
    void Initialize(
        UObjectPoolSubsystem* InOwnerSubsystem,
        FActorPoolManager* InActorManager,
        FUObjectPoolManager* InObjectManager
    );

    /**
     * @brief 反初始化并注销引擎内存修剪委托
     */
    void Deinitialize();

    /**
     * @brief 强制释放当前所有池中的闲置空闲实例
     * @param bPreserveMinCount 是否保留各池配置的 MinPreservedCount 保底实例数
     * @return 实际销毁并释放的空闲实例总数
     */
    int32 FlushAllIdleInstances(bool bPreserveMinCount = false);

    /**
     * @brief 设置是否启用在操作系统/引擎内存紧张时自动紧急泄洪缩容
     * @param bEnable true 为开启（默认开启）
     */
    void SetLowMemoryAutoFlushEnabled(bool bEnable) noexcept;

    /**
     * @brief 查询当前低内存自动泄洪保护是否开启
     * @return 若开启返回 true
     */
    [[nodiscard]] bool IsLowMemoryAutoFlushEnabled() const noexcept;

private:
    void OnMemoryTrim();

private:
    TWeakObjectPtr<UObjectPoolSubsystem> OwnerSubsystemWeak;
    FActorPoolManager* ActorManager = nullptr;
    FUObjectPoolManager* ObjectManager = nullptr;
    FDelegateHandle MemoryTrimDelegateHandle;
    bool bLowMemoryAutoFlushEnabled = true;
};
