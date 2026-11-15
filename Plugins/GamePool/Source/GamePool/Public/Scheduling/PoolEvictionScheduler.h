// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "UObject/WeakObjectPtr.h"

class UWorld;
class FActorPoolManager;
class FUObjectPoolManager;

/**
 * @brief 超时冷数据淘汰调度控制器，负责心跳定时器维护、步长销毁配额与跨池淘汰执行
 */
class GAMEPOOL_API FPoolEvictionScheduler {
public:
    FPoolEvictionScheduler() = default;
    ~FPoolEvictionScheduler() {
        Deinitialize();
    }

    FPoolEvictionScheduler(const FPoolEvictionScheduler&) = delete;
    FPoolEvictionScheduler& operator=(const FPoolEvictionScheduler&) = delete;

    /**
     * @brief 初始化淘汰调度器上下文
     * @param InWorld 世界上下文指针
     * @param InActorManager Actor 池管理器
     * @param InObjectManager UObject 池管理器
     */
    void Initialize(UWorld* InWorld, FActorPoolManager* InActorManager, FUObjectPoolManager* InObjectManager);

    /**
     * @brief 反初始化并清理定时器
     */
    void Deinitialize();

    /**
     * @brief 启动后台心跳平滑淘汰定时器
     * @param IntervalSeconds 检测周期（秒，默认 5.0s）
     */
    void StartEvictionTimer(float IntervalSeconds = 5.0f);

    /**
     * @brief 终止后台心跳淘汰定时器
     */
    void StopEvictionTimer();

    /**
     * @brief 心跳定时器触发单次回调
     */
    void OnTimerTick();

    /**
     * @brief 强制触发一次全部池的超时冷实例淘汰
     * @param MaxEvictionsPerStep 单次最大销毁配额（<=0 使用默认值 32）
     * @param CustomNowSeconds 自定义时间戳（<=0 使用当前硬件单调时间戳）
     * @return 实际销毁的冷实例总数
     */
    int32 ForceEvictExpired(int32 MaxEvictionsPerStep = 32, double CustomNowSeconds = 0.0);

    /**
     * @brief 查询当前后台淘汰定时器是否正在运行
     * @return 若激活返回 true
     */
    [[nodiscard]] bool IsTimerActive() const noexcept;

private:
    TWeakObjectPtr<UWorld> WorldPtr;
    FActorPoolManager* ActorManager = nullptr;
    FUObjectPoolManager* ObjectManager = nullptr;
    FTimerHandle EvictionTimerHandle;
};
