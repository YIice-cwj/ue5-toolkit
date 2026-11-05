// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameNetCore.h"

#include "Modules/ModuleManager.h"
#include "Logging/GameLogCategories.h"

GAME_REGISTER_MODULE_LOG(LogGameNetCore);

/**
 * @brief 通用网络契约与单机回环底座运行时模块
 */
class FGameNetCoreModule : public FDefaultModuleImpl {
public:
    FGameNetCoreModule() = default;

    FGameNetCoreModule(const FGameNetCoreModule&) = delete;
    FGameNetCoreModule& operator=(const FGameNetCoreModule&) = delete;
    FGameNetCoreModule(FGameNetCoreModule&&) = delete;
    FGameNetCoreModule& operator=(FGameNetCoreModule&&) = delete;

    /**
     * @brief 模块启动生命周期入口
     */
    void StartupModule() override {
        GAME_LOG(Log, TEXT("【GameNetCore】网络核心契约与离线回环底座初始化完成。"));
    }

    /**
     * @brief 模块卸载生命周期入口
     */
    void ShutdownModule() override {
        GAME_LOG(Log, TEXT("【GameNetCore】网络核心契约与离线回环底座卸载完成。"));
    }
};

IMPLEMENT_MODULE(FGameNetCoreModule, GameNetCore);

