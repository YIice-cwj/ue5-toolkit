// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Logging/GameLogCategories.h"

GAME_REGISTER_MODULE_LOG(LogGameData);

/**
 * @brief 数据层生成宿主模块（仅承载数据表生成产物，通用设施位于 GameDataCore）
 */
class FGameDataModule : public FDefaultModuleImpl {
public:
    FGameDataModule() = default;
    ~FGameDataModule() override = default;

    FGameDataModule(const FGameDataModule&) = delete;
    FGameDataModule& operator=(const FGameDataModule&) = delete;
    FGameDataModule(FGameDataModule&&) = delete;
    FGameDataModule& operator=(FGameDataModule&&) = delete;
};

IMPLEMENT_MODULE(FGameDataModule, GameData);
