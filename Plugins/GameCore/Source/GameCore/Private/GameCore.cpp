// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameCore.h"

#include "Modules/ModuleManager.h"

#include "Config/GameCompileSwitches.h"
#include "Registration/FGameCoreRegistration.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameCore, Log, All);

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

    /**
     * @brief 模块启动入口，注册核心层静态配置与服务并记录日志
     */
    void StartupModule() override;

    /**
     * @brief 模块卸载入口，清理核心层静态配置与服务并记录日志
     */
    void ShutdownModule() override;
};

/**
 * @brief 模块启动实现：执行基础注册并记录初始化里程碑
 */
void FGameCoreModule::StartupModule() {
    FGameCoreRegistration::RegisterAll(nullptr);
    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameCore, Log, TEXT("【GameCore】核心层主模块初始化完成。"));
    }
}

/**
 * @brief 模块卸载实现：清理基础注册并记录注销里程碑
 */
void FGameCoreModule::ShutdownModule() {
    FGameCoreRegistration::UnregisterAll(nullptr);
    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameCore, Log, TEXT("【GameCore】核心层主模块注销完成。"));
    }
}

IMPLEMENT_MODULE(FGameCoreModule, GameCore);
