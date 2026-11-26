/**
 * @file GameAssetCore.cpp
 * @brief GameAssetCore 运行时模块生命周期与日志分类注册实现
 */

#include "GameAssetCore.h"

#include "Logging/GameLogCategories.h"
#include "Pinning/FGameAssetPinner.h"

GAME_REGISTER_MODULE_LOG(LogGameAssetCore);

/**
 * @brief 模块启动初始化生命周期
 */
void FGameAssetCoreModule::StartupModule() {
    FGameAssetPinner::Startup();
    GAME_LOG(Log, TEXT("【GameAssetCore】核心资产异步流送与常驻保活基础设施模块初始化完成。"));
}

/**
 * @brief 模块关闭与清理生命周期（自动释放全部全局常驻资产句柄并注销 GC 引用器）
 */
void FGameAssetCoreModule::ShutdownModule() {
    FGameAssetPinner::Shutdown();
    GAME_LOG(Log, TEXT("【GameAssetCore】核心资产异步流送与常驻保活基础设施模块卸载完成。"));
}

IMPLEMENT_MODULE(FGameAssetCoreModule, GameAssetCore)
