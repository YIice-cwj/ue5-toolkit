// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameGameplay.h"

#include "GameGameplayLog.h"
#include "Logging/GameLogCategories.h"
#include "Modules/ModuleManager.h"

GAME_REGISTER_MODULE_LOG(LogGameGameplay);

DEFINE_LOG_CATEGORY(LogGameAbility);
DEFINE_LOG_CATEGORY(LogGameAttribute);
DEFINE_LOG_CATEGORY(LogGameDamage);
DEFINE_LOG_CATEGORY(LogGameGasBridge);

/**
 * @brief GAS 绑定子层主模块（游戏无关，零业务数值）
 *
 * 承载能力基类、通用属性集基类、伤害结算 ExecCalc 基类、GAS 引导组件、
 * 属性表现映射与 Buff/Cue 桥接；游戏层仅继承并注入业务规则与数值。
 */
class FGameGameplayModule : public FDefaultModuleImpl {
public:
    FGameGameplayModule() = default;

    FGameGameplayModule(const FGameGameplayModule&) = delete;
    FGameGameplayModule& operator=(const FGameGameplayModule&) = delete;
    FGameGameplayModule(FGameGameplayModule&&) = delete;
    FGameGameplayModule& operator=(FGameGameplayModule&&) = delete;

    /**
     * @brief 模块启动入口，记录 Gameplay 模块加载里程碑
     */
    void StartupModule() override {
        GAME_LOG(Log, TEXT("【GameGameplay】GAS 绑定与伤害管线模块初始化完成。"));
    }

    /**
     * @brief 模块卸载入口，记录 Gameplay 模块卸载里程碑
     */
    void ShutdownModule() override {
        GAME_LOG(Log, TEXT("【GameGameplay】GAS 绑定与伤害管线模块卸载完成。"));
    }
};

IMPLEMENT_MODULE(FGameGameplayModule, GameGameplay);
