// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Logging/GameLogCategories.h"

GAME_REGISTER_MODULE_LOG(LogGameDataCore);

/**
 * @brief 数据层通用核心模块（零业务字眼：仓库、JSON 装载工具与通用存档设施）
 *
 * 本模块不包含任何生成式业务域代码；生成代码位于 GameData 模块（宿主模块），
 * 示例业务位于 GameDataSamples 插件。
 */
class FGameDataCoreModule : public FDefaultModuleImpl {
public:
    FGameDataCoreModule() = default;
    ~FGameDataCoreModule() override = default;

    FGameDataCoreModule(const FGameDataCoreModule&) = delete;
    FGameDataCoreModule& operator=(const FGameDataCoreModule&) = delete;
    FGameDataCoreModule(FGameDataCoreModule&&) = delete;
    FGameDataCoreModule& operator=(FGameDataCoreModule&&) = delete;

    /**
     * @brief 模块启动入口，记录数据核心模块加载里程碑
     */
    void StartupModule() override {
        GAME_LOG(Log, TEXT("【GameDataCore】通用数据核心模块初始化完成。"));
    }

    /**
     * @brief 模块卸载入口，记录数据核心模块卸载里程碑
     */
    void ShutdownModule() override {
        GAME_LOG(Log, TEXT("【GameDataCore】通用数据核心模块卸载完成。"));
    }
};

IMPLEMENT_MODULE(FGameDataCoreModule, GameDataCore);
