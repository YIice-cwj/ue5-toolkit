/**
 * @file GameDebug.h
 * @brief GameDebug 调试与诊断插件模块主入口与全量公共契约聚合门面头
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "Cheat/FGameCheatRegistry.h"
#include "Library/UGameDebugBlueprintLibrary.h"
#include "Print/FGameDebugPrint.h"
#include "Profiling/FScopedDebugStopwatch.h"
#include "Time/FGameDebugTimeController.h"
#include "Visualizer/FGameDebugDraw.h"
#include "Visualizer/FGameDebugTrailRecorder.h"
#include "Watcher/FGameDebugWatchHub.h"

/**
 * @brief GameDebug 运行时模块生命周期管理者
 */
class FGameDebugModule : public IModuleInterface {
public:
    /**
     * @brief 模块加载启动：初始化时间控制器、调试观察中心与帧显示驱动
     */
    void StartupModule() override;

    /**
     * @brief 模块卸载清理：对称注销 Ticker、观察中心、时间控制器与全部 GM 指令
     */
    void ShutdownModule() override;

private:
    FTSTicker::FDelegateHandle TickerHandle;
};
