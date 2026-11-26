/**
 * @file GameFlow.cpp
 * @brief GameFlow 运行时模块生命周期与专属日志分类注册实现
 */

#include "GameFlow.h"

#include "Logging/GameLogCategories.h"

GAME_REGISTER_MODULE_LOG(LogGameFlow);

void FGameFlowModule::StartupModule() {
    GAME_LOG(Log, TEXT("【GameFlow】关卡流转与异步就绪屏障模块初始化完成。"));
}

void FGameFlowModule::ShutdownModule() {
    GAME_LOG(Log, TEXT("【GameFlow】关卡流转与异步就绪屏障模块卸载完成。"));
}

IMPLEMENT_MODULE(FGameFlowModule, GameFlow)
