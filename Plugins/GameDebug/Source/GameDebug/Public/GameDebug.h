/**
 * @file GameDebug.h
 * @brief GameDebug 调试与诊断插件模块主入口
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FGameDebugModule : public IModuleInterface {
public:
    /**
     * @brief 模块加载启动
     */
    virtual void StartupModule() override;

    /**
     * @brief 模块卸载清理
     */
    virtual void ShutdownModule() override;

private:
    FTSTicker::FDelegateHandle TickerHandle;
};
