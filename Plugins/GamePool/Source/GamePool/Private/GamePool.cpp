// Copyright Epic Games, Inc. All Rights Reserved.

#include "GamePool.h"

#include "Logging/GameLogCategories.h"

GAME_REGISTER_MODULE_LOG(LogGamePool);

/**
 * @brief 模块加载启动生命周期
 */
void FGamePoolModule::StartupModule() {
    GAME_LOG(Log, TEXT("【GamePool】通用对象与内存池基础设施模块初始化完成。"));
}

/**
 * @brief 模块卸载生命周期
 */
void FGamePoolModule::ShutdownModule() {
    GAME_LOG(Log, TEXT("【GamePool】通用对象与内存池基础设施模块卸载完成。"));
}

IMPLEMENT_MODULE(FGamePoolModule, GamePool)
