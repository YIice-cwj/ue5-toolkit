// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameData.h"

#include "Logging/GameLogCategories.h"
#include "Modules/ModuleManager.h"

GAME_REGISTER_MODULE_LOG(LogGameData);

/**
 * @brief 数据层业务宿主与云存档集成模块（承载数据表生成产物与 UGameCloudSaveSubsystem，通用设施位于 GameDataCore）
 */
class FGameDataModule : public FDefaultModuleImpl {
public:
    FGameDataModule() = default;
    ~FGameDataModule() override = default;

    FGameDataModule(const FGameDataModule&) = delete;
    FGameDataModule& operator=(const FGameDataModule&) = delete;
    FGameDataModule(FGameDataModule&&) = delete;
    FGameDataModule& operator=(FGameDataModule&&) = delete;

    /**
     * @brief 模块启动入口，记录数据宿主模块加载里程碑
     */
    void StartupModule() override {
        GAME_LOG(Log, TEXT("【GameData】数据宿主与云存档集成模块初始化完成。"));
    }

    /**
     * @brief 模块卸载入口，记录数据宿主模块卸载里程碑
     */
    void ShutdownModule() override {
        GAME_LOG(Log, TEXT("【GameData】数据宿主与云存档集成模块卸载完成。"));
    }
};

IMPLEMENT_MODULE(FGameDataModule, GameData);
