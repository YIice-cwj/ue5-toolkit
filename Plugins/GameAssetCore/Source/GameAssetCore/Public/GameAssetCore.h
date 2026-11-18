/**
 * @file GameAssetCore.h
 * @brief GameAssetCore 运行时模块入口声明
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * @brief GameAssetCore 核心资产模块生命周期管理类
 */
class FGameAssetCoreModule : public IModuleInterface {
public:
    /**
     * @brief 模块启动初始化
     */
    void StartupModule() override;

    /**
     * @brief 模块关闭与清理（自动释放全部全局常驻资产句柄）
     */
    void ShutdownModule() override;
};
