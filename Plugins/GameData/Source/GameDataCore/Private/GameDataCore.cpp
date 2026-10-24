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
};

IMPLEMENT_MODULE(FGameDataCoreModule, GameDataCore);
