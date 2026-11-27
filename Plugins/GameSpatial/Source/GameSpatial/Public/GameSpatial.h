/**
 * @file GameSpatial.h
 * @brief GameSpatial 纯逻辑空间算法插件模块入口与收敛公共契约聚合门面头
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "Core/SpatialErrorCodes.h"
#include "Core/SpatialGameplayTags.h"
#include "Grid/TSpatialHashGrid2D.h"
#include "Grid/TSpatialHashGrid3D.h"
#include "Subsystem/UGameSpatialSubsystem.h"
#include "Throttle/FSpatialThrottleDriver.h"
#include "Throttle/FSpatialThrottleTierConfig.h"
#include "Throttle/FSpatialViewContext.h"

/**
 * @brief GameSpatial 运行时模块生命周期管理者
 */
class FGameSpatialModule : public IModuleInterface {
public:
    /**
     * @brief 模块启动初始化生命周期
     */
    void StartupModule() override;

    /**
     * @brief 模块关闭与清理生命周期
     */
    void ShutdownModule() override;
};
