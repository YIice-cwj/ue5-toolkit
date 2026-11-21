/**
 * @file FPoolEvictionScheduler.h
 * @brief 基于 GameCore UThrottledTickerSubsystem 的超时冷实例淘汰调度控制器
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/FThrottledTickTypes.h"
#include "UObject/WeakObjectPtr.h"

class FActorPoolManager;
class FUObjectPoolManager;
class UObjectPoolSubsystem;

/**
 * @brief 超时冷数据淘汰调度控制器（按所有活跃池最小心跳周期自适应节流，全关自动停机，防单池饥饿）
 */
class FPoolEvictionScheduler {
public:
    static constexpr int32 DefaultAutoEvictGlobalQuota = 16;
    static constexpr int32 DefaultForceEvictGlobalQuota = 32;

public:
    FPoolEvictionScheduler() = default;
    ~FPoolEvictionScheduler() {
        Deinitialize();
    }

    FPoolEvictionScheduler(const FPoolEvictionScheduler&) = delete;
    FPoolEvictionScheduler& operator=(const FPoolEvictionScheduler&) = delete;

    /**
     * @brief 初始化淘汰调度器上下文
     * @param InOwnerSubsystem 所属对象池子系统指针（用作弱宿主守卫）
     * @param InActorManager Actor 池管理器
     * @param InObjectManager UObject 池管理器
     */
    void Initialize(
        UObjectPoolSubsystem* InOwnerSubsystem,
        FActorPoolManager* InActorManager,
        FUObjectPoolManager* InObjectManager
    );

    /**
     * @brief 反初始化并注销节流心跳任务
     */
    void Deinitialize();

    /**
     * @brief 按当前所有 Actor 与 UObject 池的淘汰配置动态同步心跳周期或自动停机
     */
    void SyncSchedule();

    /**
     * @brief 立即停止后台淘汰节流心跳
     */
    void StopSchedule();

    /**
     * @brief 强制触发一次全部池的超时冷实例淘汰（采用调用方指定的总配额，忽略各池单步默认限额）
     * @param MaxEvictionsPerStep 单次最大总销毁配额（<=0 使用默认值 32）
     * @param CustomNowSeconds 自定义时间戳（<=0 使用当前硬件单调时间戳）
     * @return 实际销毁的冷实例总数
     */
    int32 ForceEvictExpired(int32 MaxEvictionsPerStep = DefaultForceEvictGlobalQuota, double CustomNowSeconds = 0.0);

private:
    void OnThrottledTick(float DeltaTime);
    int32 ExecuteEvictionPass(double NowSeconds, int32 GlobalQuota, bool bRespectPerBucketLimit);

private:
    TWeakObjectPtr<UObjectPoolSubsystem> OwnerSubsystemWeak;
    FActorPoolManager* ActorManager = nullptr;
    FUObjectPoolManager* ObjectManager = nullptr;
    FThrottledTickHandle TickHandle;
    float ActiveIntervalSeconds = 0.0f;
    bool bPreferObjectsNextPass = false;
};
