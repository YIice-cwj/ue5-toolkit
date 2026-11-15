// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "Core/IGamePoolable.h"
#include "Core/ObjectPoolTypes.h"
#include "Handle/PoolGenerationTracker.h"
#include "Handle/PooledObjectHandle.h"
#include "Managers/ActorPoolManager.h"
#include "Managers/UObjectPoolManager.h"
#include "Managers/PooledActorResetter.h"
#include "Scheduling/PoolDeferredDispatcher.h"
#include "Scheduling/PoolEvictionScheduler.h"
#include "Scheduling/PoolAsyncLoader.h"
#include "Resilience/PoolResilienceController.h"
#include "Telemetry/ObjectPoolStats.h"
#include "Memory/StructPool.h"
#include "Subsystem/ObjectPoolSubsystem.h"

/**
 * @brief GamePool 插件主模块生命周期管理者
 */
class FGamePoolModule : public IModuleInterface {
public:
    /**
     * @brief 模块加载启动生命周期
     */
    virtual void StartupModule() override;

    /**
     * @brief 模块卸载生命周期
     */
    virtual void ShutdownModule() override;
};
