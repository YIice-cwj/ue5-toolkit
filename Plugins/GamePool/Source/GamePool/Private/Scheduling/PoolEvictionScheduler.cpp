// Copyright Epic Games, Inc. All Rights Reserved.

#include "Scheduling/PoolEvictionScheduler.h"
#include "Managers/ActorPoolManager.h"
#include "Managers/UObjectPoolManager.h"
#include "Engine/World.h"
#include "TimerManager.h"

/**
 * @brief 初始化淘汰调度器上下文
 * @param InWorld 世界上下文指针
 * @param InActorManager Actor 池管理器
 * @param InObjectManager UObject 池管理器
 */
void FPoolEvictionScheduler::Initialize(UWorld* InWorld, FActorPoolManager* InActorManager, FUObjectPoolManager* InObjectManager) {
    WorldPtr = InWorld;
    ActorManager = InActorManager;
    ObjectManager = InObjectManager;
}

/**
 * @brief 反初始化并清理定时器
 */
void FPoolEvictionScheduler::Deinitialize() {
    StopEvictionTimer();
    ActorManager = nullptr;
    ObjectManager = nullptr;
    WorldPtr.Reset();
}

/**
 * @brief 启动后台心跳平滑淘汰定时器
 * @param IntervalSeconds 检测周期（秒，默认 5.0s）
 */
void FPoolEvictionScheduler::StartEvictionTimer(const float IntervalSeconds) {
    if (UWorld* World = WorldPtr.Get()) {
        World->GetTimerManager().SetTimer(
            EvictionTimerHandle,
            FTimerDelegate::CreateRaw(this, &FPoolEvictionScheduler::OnTimerTick),
            FMath::Max(0.1f, IntervalSeconds),
            true
        );
    }
}

/**
 * @brief 终止后台心跳淘汰定时器
 */
void FPoolEvictionScheduler::StopEvictionTimer() {
    if (UWorld* World = WorldPtr.Get()) {
        World->GetTimerManager().ClearTimer(EvictionTimerHandle);
    }
    EvictionTimerHandle.Invalidate();
}

/**
 * @brief 心跳定时器触发单次回调
 */
void FPoolEvictionScheduler::OnTimerTick() {
    const double NowSeconds = FPlatformTime::Seconds();
    int32 EvictedActors = 0;
    if (ActorManager != nullptr) {
        EvictedActors = ActorManager->EvictExpiredActors(NowSeconds, 16);
    }
    const int32 RemainingQuota = 16 - EvictedActors;
    if (ObjectManager != nullptr && RemainingQuota > 0) {
        ObjectManager->EvictExpiredObjects(NowSeconds, RemainingQuota);
    }
}

/**
 * @brief 强制触发一次全部池的超时冷实例淘汰
 * @param MaxEvictionsPerStep 单次最大销毁配额（<=0 使用默认值 32）
 * @param CustomNowSeconds 自定义时间戳（<=0 使用当前硬件单调时间戳）
 * @return 实际销毁的冷实例总数
 */
int32 FPoolEvictionScheduler::ForceEvictExpired(const int32 MaxEvictionsPerStep, const double CustomNowSeconds) {
    const double NowSeconds = (CustomNowSeconds > 0.0) ? CustomNowSeconds : FPlatformTime::Seconds();
    const int32 Quota = (MaxEvictionsPerStep > 0) ? MaxEvictionsPerStep : 32;
    int32 EvictedActors = 0;
    int32 EvictedObjects = 0;
    if (ActorManager != nullptr) {
        EvictedActors = ActorManager->EvictExpiredActors(NowSeconds, Quota);
    }
    const int32 RemainingQuota = Quota - EvictedActors;
    if (ObjectManager != nullptr && RemainingQuota > 0) {
        EvictedObjects = ObjectManager->EvictExpiredObjects(NowSeconds, RemainingQuota);
    }
    return EvictedActors + EvictedObjects;
}

/**
 * @brief 查询当前后台淘汰定时器是否正在运行
 * @return 若激活返回 true
 */
bool FPoolEvictionScheduler::IsTimerActive() const noexcept {
    return EvictionTimerHandle.IsValid();
}
