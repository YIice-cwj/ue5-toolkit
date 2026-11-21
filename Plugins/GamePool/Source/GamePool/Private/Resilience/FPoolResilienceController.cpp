/**
 * @file FPoolResilienceController.cpp
 * @brief 内存弹性伸缩与低内存应急泄洪内部容灾控制器实现
 */

#include "Resilience/FPoolResilienceController.h"

#include "Async/FGameThreadTaskRunner.h"
#include "Logging/GameLogCategories.h"
#include "Managers/FActorPoolManager.h"
#include "Managers/FUObjectPoolManager.h"
#include "Subsystem/UObjectPoolSubsystem.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"

/**
 * @brief 初始化弹性控制器并注册引擎内存修剪委托
 * @param InOwnerSubsystem 所属子系统对象指针
 * @param InActorManager Actor 池管理器
 * @param InObjectManager UObject 池管理器
 */
void FPoolResilienceController::Initialize(
    UObjectPoolSubsystem* InOwnerSubsystem,
    FActorPoolManager* InActorManager,
    FUObjectPoolManager* InObjectManager
) {
    Deinitialize();
    OwnerSubsystemWeak = InOwnerSubsystem;
    ActorManager = InActorManager;
    ObjectManager = InObjectManager;
    MemoryTrimDelegateHandle = FCoreDelegates::GetMemoryTrimDelegate().AddRaw(this, &FPoolResilienceController::OnMemoryTrim);
}

/**
 * @brief 反初始化并注销引擎内存修剪委托
 */
void FPoolResilienceController::Deinitialize() {
    if (MemoryTrimDelegateHandle.IsValid()) {
        FCoreDelegates::GetMemoryTrimDelegate().Remove(MemoryTrimDelegateHandle);
        MemoryTrimDelegateHandle.Reset();
    }
    OwnerSubsystemWeak.Reset();
    ActorManager = nullptr;
    ObjectManager = nullptr;
}

/**
 * @brief 强制释放当前所有池中的闲置空闲实例
 * @param bPreserveMinCount 是否保留各池配置的 MinPreservedCount 保底实例数
 * @return 实际销毁并释放的空闲实例总数
 */
int32 FPoolResilienceController::FlushAllIdleInstances(const bool bPreserveMinCount) {
    int32 FlushedTotal = 0;
    if (ActorManager != nullptr) {
        FlushedTotal += ActorManager->FlushIdleActors(bPreserveMinCount);
    }
    if (ObjectManager != nullptr) {
        FlushedTotal += ObjectManager->FlushIdleObjects(bPreserveMinCount);
    }
    return FlushedTotal;
}

/**
 * @brief 设置是否启用在操作系统/引擎内存紧张时自动紧急泄洪缩容
 * @param bEnable true 为开启（默认开启）
 */
void FPoolResilienceController::SetLowMemoryAutoFlushEnabled(const bool bEnable) noexcept {
    bLowMemoryAutoFlushEnabled = bEnable;
}

/**
 * @brief 查询当前低内存自动泄洪保护是否开启
 * @return 若开启返回 true
 */
bool FPoolResilienceController::IsLowMemoryAutoFlushEnabled() const noexcept {
    return bLowMemoryAutoFlushEnabled;
}

/**
 * @brief 响应操作系统或引擎低内存修剪警报（非 GameThread 触发时切回主线程，且跳过常规 GC/增量清理内部的 FMemory::Trim 广播）
 */
void FPoolResilienceController::OnMemoryTrim() {
    if (!bLowMemoryAutoFlushEnabled) {
        return;
    }

    if (!IsInGameThread()) {
        TWeakObjectPtr<UObjectPoolSubsystem> WeakSubsystem = OwnerSubsystemWeak;
        FGameThreadTaskRunner::RunOnGameThread([WeakSubsystem]() {
            UObjectPoolSubsystem* Subsystem = WeakSubsystem.Get();
            if (Subsystem == nullptr || !Subsystem->IsLowMemoryAutoFlushEnabled()) {
                return;
            }
            if (IsGarbageCollecting() || IsIncrementalPurgePending()) {
                return;
            }
            Subsystem->FlushAllIdleInstances(true);
        });
        return;
    }

    if (IsGarbageCollecting() || IsIncrementalPurgePending()) {
        return;
    }

    GAME_LOG(Log, TEXT("【对象池】检测到内存修剪警报 (MemoryTrim)，触发保底紧急泄洪！"));
    FlushAllIdleInstances(true);
}
