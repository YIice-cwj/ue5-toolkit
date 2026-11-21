/**
 * @file FPoolEvictionScheduler.cpp
 * @brief 基于 GameCore UThrottledTickerSubsystem 的超时冷实例淘汰调度控制器实现
 */

#include "Scheduling/FPoolEvictionScheduler.h"

#include "Async/UThrottledTickerSubsystem.h"
#include "HAL/PlatformTime.h"
#include "Managers/FActorPoolManager.h"
#include "Managers/FUObjectPoolManager.h"
#include "Subsystem/UObjectPoolSubsystem.h"

/**
 * @brief 初始化淘汰调度器上下文
 * @param InOwnerSubsystem 所属对象池子系统指针
 * @param InActorManager Actor 池管理器
 * @param InObjectManager UObject 池管理器
 */
void FPoolEvictionScheduler::Initialize(
    UObjectPoolSubsystem* InOwnerSubsystem,
    FActorPoolManager* InActorManager,
    FUObjectPoolManager* InObjectManager
) {
    Deinitialize();
    OwnerSubsystemWeak = InOwnerSubsystem;
    ActorManager = InActorManager;
    ObjectManager = InObjectManager;
}

/**
 * @brief 反初始化并注销节流心跳任务
 */
void FPoolEvictionScheduler::Deinitialize() {
    StopSchedule();
    ActorManager = nullptr;
    ObjectManager = nullptr;
    OwnerSubsystemWeak.Reset();
    bPreferObjectsNextPass = false;
}

/**
 * @brief 按当前所有 Actor 与 UObject 池的淘汰配置动态同步心跳周期或自动停机
 */
void FPoolEvictionScheduler::SyncSchedule() {
    float MinInterval = MAX_flt;
    bool bAnyEnabled = false;

    float ActorMin = MAX_flt;
    if (ActorManager != nullptr && ActorManager->GetMinimumEvictionCheckIntervalSeconds(ActorMin)) {
        MinInterval = FMath::Min(MinInterval, ActorMin);
        bAnyEnabled = true;
    }

    float ObjectMin = MAX_flt;
    if (ObjectManager != nullptr && ObjectManager->GetMinimumEvictionCheckIntervalSeconds(ObjectMin)) {
        MinInterval = FMath::Min(MinInterval, ObjectMin);
        bAnyEnabled = true;
    }

    if (!bAnyEnabled) {
        StopSchedule();
        return;
    }

    const float ClampedInterval = FMath::Max(0.1f, MinInterval);
    if (TickHandle.IsActive()) {
        if (!FMath::IsNearlyEqual(ActiveIntervalSeconds, ClampedInterval)) {
            TickHandle.SetInterval(ClampedInterval);
            ActiveIntervalSeconds = ClampedInterval;
        }
        return;
    }

    UObjectPoolSubsystem* OwnerSubsystem = OwnerSubsystemWeak.Get();
    UThrottledTickerSubsystem* TickerSubsystem = UThrottledTickerSubsystem::GetPtr(OwnerSubsystem);
    if (OwnerSubsystem == nullptr || TickerSubsystem == nullptr) {
        return;
    }

    TickHandle = TickerSubsystem->RegisterIntervalTick(
        TWeakObjectPtr<UObject>(OwnerSubsystem),
        ClampedInterval,
        [this](const float DeltaTime) {
            OnThrottledTick(DeltaTime);
        }
    );
    ActiveIntervalSeconds = TickHandle.IsActive() ? ClampedInterval : 0.0f;
}

/**
 * @brief 立即停止后台淘汰节流心跳
 */
void FPoolEvictionScheduler::StopSchedule() {
    if (TickHandle.IsActive()) {
        TickHandle.Unregister();
    }
    ActiveIntervalSeconds = 0.0f;
}

/**
 * @brief 强制触发一次全部池的超时冷实例淘汰
 * @param MaxEvictionsPerStep 单次最大总销毁配额（<=0 使用默认值 32）
 * @param CustomNowSeconds 自定义时间戳（<=0 使用当前硬件单调时间戳）
 * @return 实际销毁的冷实例总数
 */
int32 FPoolEvictionScheduler::ForceEvictExpired(const int32 MaxEvictionsPerStep, const double CustomNowSeconds) {
    const double NowSeconds = (CustomNowSeconds > 0.0) ? CustomNowSeconds : FPlatformTime::Seconds();
    const int32 Quota = (MaxEvictionsPerStep > 0) ? MaxEvictionsPerStep : DefaultForceEvictGlobalQuota;
    return ExecuteEvictionPass(NowSeconds, Quota, false);
}

/**
 * @brief 节流心跳回调：按各池 MaxEvictionsPerStep 平滑淘汰冷实例，并在 Actor 与 UObject 之间轮转防饥饿
 * @param DeltaTime 距上次心跳的累积流逝时间
 */
void FPoolEvictionScheduler::OnThrottledTick([[maybe_unused]] const float DeltaTime) {
    (void)ExecuteEvictionPass(FPlatformTime::Seconds(), DefaultAutoEvictGlobalQuota, true);
}

/**
 * @brief 执行一轮跨 Actor 与 UObject 池的配额淘汰
 * @param NowSeconds 当前单调时间戳（秒）
 * @param GlobalQuota 全局最大销毁配额
 * @param bRespectPerBucketLimit 是否受各池 MaxEvictionsPerStep 约束
 * @return 实际销毁的实例总数
 */
int32 FPoolEvictionScheduler::ExecuteEvictionPass(
    const double NowSeconds,
    const int32 GlobalQuota,
    const bool bRespectPerBucketLimit
) {
    int32 EvictedFirst = 0;
    int32 EvictedSecond = 0;

    if (!bPreferObjectsNextPass) {
        if (ActorManager != nullptr) {
            EvictedFirst = ActorManager->EvictExpiredActors(NowSeconds, GlobalQuota, bRespectPerBucketLimit);
        }
        const int32 Remaining = GlobalQuota - EvictedFirst;
        if (ObjectManager != nullptr && Remaining > 0) {
            EvictedSecond = ObjectManager->EvictExpiredObjects(NowSeconds, Remaining, bRespectPerBucketLimit);
        }
    } else {
        if (ObjectManager != nullptr) {
            EvictedFirst = ObjectManager->EvictExpiredObjects(NowSeconds, GlobalQuota, bRespectPerBucketLimit);
        }
        const int32 Remaining = GlobalQuota - EvictedFirst;
        if (ActorManager != nullptr && Remaining > 0) {
            EvictedSecond = ActorManager->EvictExpiredActors(NowSeconds, Remaining, bRespectPerBucketLimit);
        }
    }

    bPreferObjectsNextPass = !bPreferObjectsNextPass;
    return EvictedFirst + EvictedSecond;
}
