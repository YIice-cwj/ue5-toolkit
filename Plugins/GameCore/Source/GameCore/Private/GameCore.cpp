// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameCore.h"

#include "Modules/ModuleManager.h"

#include "Registration/FGameCoreRegistration.h"

/**
 * @brief 核心层主模块
 *
 * 编译开关桥接必须前置到模块加载点：早于任何 Gameplay 读取，与 GameInstance 生命周期解耦。
 */
class FGameCoreModule : public FDefaultModuleImpl {
public:
    FGameCoreModule() = default;
    ~FGameCoreModule() = default;

    FGameCoreModule(const FGameCoreModule&) = delete;
    FGameCoreModule& operator=(const FGameCoreModule&) = delete;
    FGameCoreModule(FGameCoreModule&&) = delete;
    FGameCoreModule& operator=(FGameCoreModule&&) = delete;

    void StartupModule() override;

    void ShutdownModule() override;
};

void FGameCoreModule::StartupModule() {
    FGameCoreRegistration::RegisterAll(nullptr);
}

void FGameCoreModule::ShutdownModule() {
    FGameCoreRegistration::UnregisterAll(nullptr);
}

IMPLEMENT_MODULE(FGameCoreModule, GameCore);
