/**
 * @file GameDebug.cpp
 * @brief GameDebug 调试与诊断插件模块入口与日志分类注册实现
 */

#include "GameDebug.h"

#include "Config/GameCompileSwitches.h"
#include "Containers/Ticker.h"
#include "Logging/GameLogCategories.h"

GAME_REGISTER_MODULE_LOG(LogGameDebug);

void FGameDebugModule::StartupModule() {
    FGameDebugTimeController::Initialize();
    FGameDebugWatchHub::Get().Initialize();

    if constexpr (Game::Config::bEnableDebugDraw) {
        TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateLambda([](float DeltaTime) -> bool {
                FGameDebugWatchHub::Get().TickDisplay(DeltaTime);
                return true;
            })
        );
    }

    GAME_LOG(Log, TEXT("【GameDebug】模块化调试、GM 指令、屏幕诊断与 3D 可视化基础设施模块初始化完成。"));
}

void FGameDebugModule::ShutdownModule() {
    if constexpr (Game::Config::bEnableDebugDraw) {
        if (TickerHandle.IsValid()) {
            FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
            TickerHandle.Reset();
        }
    }

    FGameDebugWatchHub::Get().Shutdown();
    FGameDebugTimeController::Shutdown();
    FGameCheatRegistry::Get().ClearAll();

    GAME_LOG(Log, TEXT("【GameDebug】模块化调试、GM 指令、屏幕诊断与 3D 可视化基础设施模块卸载完成。"));
}

IMPLEMENT_MODULE(FGameDebugModule, GameDebug)
