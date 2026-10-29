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

    /**
     * @brief 模块启动入口，记录数据示例模块加载里程碑
     */
    void StartupModule() override {
        GAME_LOG(Log, TEXT("【GameDataSamples】数据示例模块初始化完成。"));
    }

    /**
     * @brief 模块卸载入口，记录数据示例模块卸载里程碑
     */
    void ShutdownModule() override {
        GAME_LOG(Log, TEXT("【GameDataSamples】数据示例模块卸载完成。"));
    }
};

IMPLEMENT_MODULE(FGameDataSamplesModule, GameDataSamples);
