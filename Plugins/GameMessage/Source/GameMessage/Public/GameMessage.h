// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

/**
 * @brief GameMessage 模块运行时入口
 */
class FGameMessageModule : public IModuleInterface {
public:
    void StartupModule() override;
    void ShutdownModule() override;
};
