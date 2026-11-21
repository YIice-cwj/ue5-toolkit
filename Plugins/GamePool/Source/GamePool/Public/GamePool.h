/**
 * @file GamePool.h
 * @brief GamePool 插件主模块入口与公共对外契约聚合头
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "Core/FPoolAcquirePayload.h"
#include "Core/IGamePoolable.h"
#include "Core/ObjectPoolTypes.h"
#include "Core/PoolErrorCodes.h"
#include "Handle/TPooledObjectHandle.h"
#include "Handle/TScopedPoolRental.h"
#include "Memory/TScopedStructRental.h"
#include "Memory/TStructPool.h"
#include "Subsystem/UObjectPoolSubsystem.h"
#include "Telemetry/ObjectPoolStats.h"

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
