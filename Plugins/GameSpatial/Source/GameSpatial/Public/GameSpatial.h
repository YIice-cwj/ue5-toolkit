/**
 * @file GameSpatial.h
 * @brief GameSpatial 纯逻辑空间算法模块入口声明
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * @brief GameSpatial 运行时模块生命周期类
 */
class FGameSpatialModule : public IModuleInterface {
public:
    /**
     * @brief 模块启动初始化
     */
    void StartupModule() override;

    /**
     * @brief 模块关闭与清理
     */
    void ShutdownModule() override;
};
