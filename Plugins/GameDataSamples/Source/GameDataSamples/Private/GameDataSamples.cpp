// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Logging/GameLogCategories.h"

GAME_REGISTER_MODULE_LOG(LogGameDataSamples);

/**
 * @brief 数据层示例模块（演示业务域与玩家档案持久化，不随通用安装部署）
 */
class FGameDataSamplesModule : public FDefaultModuleImpl {
public:
    FGameDataSamplesModule() = default;

    FGameDataSamplesModule(const FGameDataSamplesModule&) = delete;
    FGameDataSamplesModule& operator=(const FGameDataSamplesModule&) = delete;
    FGameDataSamplesModule(FGameDataSamplesModule&&) = delete;
    FGameDataSamplesModule& operator=(FGameDataSamplesModule&&) = delete;
};

IMPLEMENT_MODULE(FGameDataSamplesModule, GameDataSamples);
