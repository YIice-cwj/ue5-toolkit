/**
 * @file GameMessage.cpp
 * @brief GameMessage 插件模块运行时入口与日志分类定义
 */

#include "GameMessage.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogGameMessage);

/**
 * @brief 模块启动生命周期回调
 */
void FGameMessageModule::StartupModule() {
}

/**
 * @brief 模块关闭生命周期回调
 */
void FGameMessageModule::ShutdownModule() {
}

IMPLEMENT_MODULE(FGameMessageModule, GameMessage)
