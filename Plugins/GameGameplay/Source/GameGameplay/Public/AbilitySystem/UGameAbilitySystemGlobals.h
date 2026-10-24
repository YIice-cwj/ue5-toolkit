/**
 * @file UGameAbilitySystemGlobals.h
 * @brief 能力系统全局单例（本工程 GameplayEffectContext 供给方）
 *
 * 设计思路：UE 5.8 中 AllocGameplayEffectContext 是唯一官方扩展点，引擎默认返回原生
 * FGameplayEffectContext；本类改为返回框架层 FGameGameplayEffectContext，使 GE 施加链路
 * 自动携带打击反馈字段。生效方式为项目配置显式指向（Config/DefaultGame.ini 的
 * GameplayAbilitiesDeveloperSettings.AbilitySystemGlobalsClassName）——仅启用插件不改变
 * 引擎默认行为，属显式 opt-in。
 */

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemGlobals.h"

#include "GameGameplay.h"
#include "UGameAbilitySystemGlobals.generated.h"

/**
 * @brief 能力系统全局单例（游戏无关）
 */
UCLASS()
class GAMEGAMEPLAY_API UGameAbilitySystemGlobals : public UAbilitySystemGlobals {
    GENERATED_BODY()

public:
    FGameplayEffectContext* AllocGameplayEffectContext() const override;
};
