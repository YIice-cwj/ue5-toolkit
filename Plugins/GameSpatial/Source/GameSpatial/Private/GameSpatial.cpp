/**
 * @file GameSpatial.cpp
 * @brief GameSpatial 纯逻辑空间算法模块入口与日志分类注册实现
 */

#include "GameSpatial.h"

#include "Logging/GameLogCategories.h"

GAME_REGISTER_MODULE_LOG(LogGameSpatial);

/**
 * @brief 模块启动初始化生命周期
 */
void FGameSpatialModule::StartupModule() {
    GAME_LOG(Log, TEXT("【GameSpatial】纯逻辑空间哈希索引、世界图层子系统与视锥节流基础设施模块初始化完成。"));
}

/**
 * @brief 模块关闭与清理生命周期
 */
void FGameSpatialModule::ShutdownModule() {
    GAME_LOG(Log, TEXT("【GameSpatial】纯逻辑空间哈希索引、世界图层子系统与视锥节流基础设施模块卸载完成。"));
}

IMPLEMENT_MODULE(FGameSpatialModule, GameSpatial)
