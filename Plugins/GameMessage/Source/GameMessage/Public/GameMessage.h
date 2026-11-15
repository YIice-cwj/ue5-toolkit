// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

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
