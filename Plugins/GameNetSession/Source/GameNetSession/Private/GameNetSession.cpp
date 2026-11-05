// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameNetSession.h"

#include "Modules/ModuleManager.h"
#include "Logging/GameLogCategories.h"

GAME_REGISTER_MODULE_LOG(LogGameNetSession);

/**
 * @brief 房间会话、局域网自动发现与联机握手运行时模块
 */
class FGameNetSessionModule : public FDefaultModuleImpl {
public:
    FGameNetSessionModule() = default;

    FGameNetSessionModule(const FGameNetSessionModule&) = delete;
    FGameNetSessionModule& operator=(const FGameNetSessionModule&) = delete;
    FGameNetSessionModule(FGameNetSessionModule&&) = delete;
    FGameNetSessionModule& operator=(FGameNetSessionModule&&) = delete;

    /**
     * @brief 模块启动生命周期入口
     */
    void StartupModule() override {
        GAME_LOG(Log, TEXT("【GameNetSession】房间会话与局域网发现模块初始化完成。"));
    }

    /**
     * @brief 模块卸载生命周期入口
     */
    void ShutdownModule() override {
        GAME_LOG(Log, TEXT("【GameNetSession】房间会话与局域网发现模块卸载完成。"));
    }
};

IMPLEMENT_MODULE(FGameNetSessionModule, GameNetSession);

