// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameGameplay.h"

#include "Modules/ModuleManager.h"
#include "Logging/GameLogCategories.h"

GAME_REGISTER_MODULE_LOG(LogGameGameplay);

/**
 * @brief GAS 绑定子层主模块（游戏无关，零业务数值）
 *
 * 承载能力基类、属性集基类（双轨元属性 + 钳制/幂等工具）、伤害结算 ExecCalc 基类
 * 与属性表现契约工具；游戏层仅继承并"填数"，框架层不感知任何业务字段与数值。
 */
class FGameGameplayModule : public FDefaultModuleImpl {
public:
    FGameGameplayModule() = default;

    FGameGameplayModule(const FGameGameplayModule&) = delete;
    FGameGameplayModule& operator=(const FGameGameplayModule&) = delete;
    FGameGameplayModule(FGameGameplayModule&&) = delete;
    FGameGameplayModule& operator=(FGameGameplayModule&&) = delete;
};

IMPLEMENT_MODULE(FGameGameplayModule, GameGameplay);
