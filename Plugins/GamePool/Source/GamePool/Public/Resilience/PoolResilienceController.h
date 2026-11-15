// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/CoreDelegates.h"

class FActorPoolManager;
class FUObjectPoolManager;

/**
 * @brief 内存弹性伸缩与容灾防御控制器，监听系统低内存修剪告警并调度紧急泄洪
 */
class GAMEPOOL_API FPoolResilienceController {
public:
    FPoolResilienceController() = default;
    ~FPoolResilienceController() {
        Deinitialize();
    }

    FPoolResilienceController(const FPoolResilienceController&) = delete;
    FPoolResilienceController& operator=(const FPoolResilienceController&) = delete;

    /**
     * @brief 初始化弹性控制器并注册引擎内存修剪委托
     * @param InActorManager Actor 池管理器
     * @param InObjectManager UObject 池管理器
     */
    void Initialize(FActorPoolManager* InActorManager, FUObjectPoolManager* InObjectManager);

    /**
     * @brief 反初始化并注销引擎内存修剪委托
     */
    void Deinitialize();

    /**
     * @brief 强制释放当前所有池中的全部闲置空闲实例
     * @param bPreserveMinCount 是否保留各池配置的最小保底实例数
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

    /**
     * @brief 响应操作系统或引擎低内存修剪警报的回调
     */
    void OnMemoryTrim();

private:
    FActorPoolManager* ActorManager = nullptr;
    FUObjectPoolManager* ObjectManager = nullptr;
    FDelegateHandle MemoryTrimDelegateHandle;
    bool bLowMemoryAutoFlushEnabled = true;
};
