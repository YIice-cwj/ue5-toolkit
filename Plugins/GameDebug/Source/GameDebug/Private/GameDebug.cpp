/**
 * @file GameDebug.cpp
 * @brief GameDebug 调试与诊断插件模块实现
 */

#include "GameDebug.h"
#include "Cheat/FGameCheatRegistry.h"
#include "Watcher/FGameDebugWatchHub.h"
#include "Containers/Ticker.h"

#define LOCTEXT_NAMESPACE "FGameDebugModule"

void FGameDebugModule::StartupModule() {
#if !UE_BUILD_SHIPPING
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateLambda([](float DeltaTime) -> bool {
            FGameDebugWatchHub::Get().TickDisplay(DeltaTime);
            return true;
        })
    );
#endif
}

void FGameDebugModule::ShutdownModule() {
#if !UE_BUILD_SHIPPING
    if (TickerHandle.IsValid()) {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle.Reset();
    }
#endif
    FGameDebugWatchHub::Get().ClearAll();
    FGameCheatRegistry::Get().ClearAll();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGameDebugModule, GameDebug)
