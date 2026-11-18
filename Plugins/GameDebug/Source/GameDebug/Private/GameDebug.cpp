/**
 * @file GameDebug.cpp
 * @brief GameDebug 调试与诊断插件模块实现
 */

#include "GameDebug.h"

#include "Cheat/FGameCheatRegistry.h"
#include "Config/GameCompileSwitches.h"
#include "Containers/Ticker.h"
#include "Time/FGameDebugTimeController.h"
#include "Watcher/FGameDebugWatchHub.h"

#define LOCTEXT_NAMESPACE "FGameDebugModule"

void FGameDebugModule::StartupModule() {
    FGameDebugTimeController::Initialize();

    if constexpr (Game::Config::bEnableDebugDraw) {
        TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateLambda([](float DeltaTime) -> bool {
                FGameDebugWatchHub::Get().TickDisplay(DeltaTime);
                return true;
            })
        );
    }
}

void FGameDebugModule::ShutdownModule() {
    if constexpr (Game::Config::bEnableDebugDraw) {
        if (TickerHandle.IsValid()) {
            FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
            TickerHandle.Reset();
        }
    }
    FGameDebugWatchHub::Get().ClearAll();
    FGameCheatRegistry::Get().ClearAll();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGameDebugModule, GameDebug)
