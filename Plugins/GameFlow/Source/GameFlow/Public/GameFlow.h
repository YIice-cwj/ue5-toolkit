/**
 * @file GameFlow.h
 * @brief GameFlow 关卡流转与异步就绪屏障运行时模块接口声明
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * @brief GameFlow 运行时模块入口类
 */
class FGameFlowModule : public IModuleInterface {
public:
    /**
     * @brief 模块启动生命周期回调
     */
    void StartupModule() override;

    /**
     * @brief 模块卸载生命周期回调
     */
    void ShutdownModule() override;
};
