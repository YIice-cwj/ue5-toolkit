// Copyright Epic Games, Inc. All Rights Reserved.

#include "Resilience/PoolResilienceController.h"
#include "Managers/ActorPoolManager.h"
#include "Managers/UObjectPoolManager.h"
#include "Config/GameCompileSwitches.h"

namespace {

DEFINE_LOG_CATEGORY_STATIC(LogGamePoolResilience, Log, All);

}  // namespace

/**
 * @brief 初始化弹性控制器并注册引擎内存修剪委托
 * @param InActorManager Actor 池管理器
 * @param InObjectManager UObject 池管理器
 */
void FPoolResilienceController::Initialize(FActorPoolManager* InActorManager, FUObjectPoolManager* InObjectManager) {
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
    ActorManager = nullptr;
    ObjectManager = nullptr;
}

/**
 * @brief 强制释放当前所有池中的全部闲置空闲实例
 * @param bPreserveMinCount 是否保留各池配置的最小保底实例数
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
 * @brief 响应操作系统或引擎低内存修剪警报的回调
 */
void FPoolResilienceController::OnMemoryTrim() {
    if (!bLowMemoryAutoFlushEnabled) {
        return;
    }
    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGamePoolResilience, Log, TEXT("【对象池】检测到内存修剪警报 (MemoryTrim)，触发紧急泄洪！"));
    }
    FlushAllIdleInstances(true);
}
