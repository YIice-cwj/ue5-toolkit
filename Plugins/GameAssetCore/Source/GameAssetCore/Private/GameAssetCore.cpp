// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameAssetCore.h"
#include "Asset/FGameAssetLoader.h"

#define LOCTEXT_NAMESPACE "FGameAssetCoreModule"

void FGameAssetCoreModule::StartupModule() {
}

void FGameAssetCoreModule::ShutdownModule() {
    FGameAssetLoader::ClearAllPinnedAssets();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGameAssetCoreModule, GameAssetCore)
