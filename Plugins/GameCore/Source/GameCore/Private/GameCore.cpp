// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameCore.h"

#include "Modules/ModuleManager.h"

#include "Config/GameCompileSwitches.h"
#include "Config/FGameCoreRegistration.h"
#include "Async/FGameThreadTaskRunner.h"
#include "State/TGameStateMachine.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameCore, Log, All);
DEFINE_LOG_CATEGORY(LogGameStateMachine);

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
     * @brief 模块启动入口：向全局 FConfigRegistry 注册核心编译期桥接开关并输出启动日志
     */
    void StartupModule() override;

    /**
     * @brief 模块卸载入口：取消排队中的后台任务、排空在飞异步任务并执行对称注销
     */
    void ShutdownModule() override;
};

/**
 * @brief 模块启动实现：注册编译开关桥接键并记录初始化里程碑
 */
void FGameCoreModule::StartupModule() {
    FGameCoreRegistration::RegisterAll();
    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameCore, Log, TEXT("【GameCore】核心层主模块初始化完成。"));
    }
}

/**
 * @brief 模块卸载实现：取消未开始的排队后台任务、等待在飞异步任务排空并执行对称注销
 */
void FGameCoreModule::ShutdownModule() {
    FGameThreadTaskRunner::CancelAllPendingTasks();
    FGameThreadTaskRunner::FlushInFlightTasks(1.0);
    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameCore, Log, TEXT("【GameCore】核心层主模块注销完成。"));
    }
}

IMPLEMENT_MODULE(FGameCoreModule, GameCore);
