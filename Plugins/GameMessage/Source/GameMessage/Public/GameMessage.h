/**
 * @file GameMessage.h
 * @brief GameMessage 插件模块运行时入口与共享日志分类声明
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

GAMEMESSAGE_API DECLARE_LOG_CATEGORY_EXTERN(LogGameMessage, Log, All);

/**
 * @brief GameMessage 模块运行时入口
 */
class FGameMessageModule : public IModuleInterface {
public:
    /**
     * @brief 模块启动生命周期回调
     */
    void StartupModule() override;

    /**
     * @brief 模块关闭生命周期回调
     */
    void ShutdownModule() override;
};
