// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameNetService.h"

#include "Modules/ModuleManager.h"
#include "Logging/GameLogCategories.h"

GAME_REGISTER_MODULE_LOG(LogGameNetService);

/**
 * @brief 业务长连接网络服务运行时模块
 */
class FGameNetServiceModule : public FDefaultModuleImpl {
public:
    FGameNetServiceModule() = default;

    FGameNetServiceModule(const FGameNetServiceModule&) = delete;
    FGameNetServiceModule& operator=(const FGameNetServiceModule&) = delete;
    FGameNetServiceModule(FGameNetServiceModule&&) = delete;
    FGameNetServiceModule& operator=(FGameNetServiceModule&&) = delete;

    /**
     * @brief 模块启动生命周期入口
     */
    void StartupModule() override {
        GAME_LOG(Log, TEXT("【GameNetService】业务长连接服务模块初始化完成。"));
    }

    /**
     * @brief 模块卸载生命周期入口
     */
    void ShutdownModule() override {
        GAME_LOG(Log, TEXT("【GameNetService】业务长连接服务模块卸载完成。"));
    }
};

IMPLEMENT_MODULE(FGameNetServiceModule, GameNetService);

