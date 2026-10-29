// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameGameplayCore.h"

#include "Modules/ModuleManager.h"
#include "Logging/GameLogCategories.h"

GAME_REGISTER_MODULE_LOG(LogGameGameplayCore);

/**
 * @brief 战斗框架核心主模块（零 GAS，游戏无关）
 *
 * 承载伤害分发契约（FGameDamageContext / FGameDamageResult / IDamageable）
 * 与通用数值状态机（UGameStatComponent 双模自适应）；表现层与 GAS 绑定层见 GameGameplay 插件。
 */
class FGameGameplayCoreModule : public FDefaultModuleImpl {
public:
    FGameGameplayCoreModule() = default;

    FGameGameplayCoreModule(const FGameGameplayCoreModule&) = delete;
    FGameGameplayCoreModule& operator=(const FGameGameplayCoreModule&) = delete;
    FGameGameplayCoreModule(FGameGameplayCoreModule&&) = delete;
    FGameGameplayCoreModule& operator=(FGameGameplayCoreModule&&) = delete;

    /**
     * @brief 模块启动入口，记录战斗核心模块加载里程碑
     */
    void StartupModule() override {
        GAME_LOG(Log, TEXT("【GameGameplayCore】战斗核心模块初始化完成。"));
    }

    /**
     * @brief 模块卸载入口，记录战斗核心模块卸载里程碑
     */
    void ShutdownModule() override {
        GAME_LOG(Log, TEXT("【GameGameplayCore】战斗核心模块卸载完成。"));
    }
};

IMPLEMENT_MODULE(FGameGameplayCoreModule, GameGameplayCore);
