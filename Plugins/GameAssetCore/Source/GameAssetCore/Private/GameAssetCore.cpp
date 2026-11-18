/**
 * @file GameAssetCore.cpp
 * @brief GameAssetCore 运行时模块生命周期实现
 */

#include "GameAssetCore.h"
#include "Asset/FGameAssetPinner.h"

void FGameAssetCoreModule::StartupModule() {
}

void FGameAssetCoreModule::ShutdownModule() {
    FGameAssetPinner::ClearAllPinnedAssets();
}

IMPLEMENT_MODULE(FGameAssetCoreModule, GameAssetCore)
