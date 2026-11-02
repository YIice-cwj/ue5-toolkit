/**
 * @file GameFeedbackPerspective.h
 * @brief 本地玩家观察视角判定（供各监听者复用）
 */

#pragma once

#include "CoreMinimal.h"
#include "Feedback/GameFeedbackSignal.h"

class APlayerController;

namespace GameFeedback {

/**
 * @brief 判定指定本地玩家在本次表现中的观察视角
 * @param LocalPC 本地玩家控制器
 * @param Instigator 动作发起者
 * @param DamageCauser 伤害造成实体
 * @param Target 目标实体
 * @return 视角分类
 */
EGameFeedbackPerspective ResolvePerspective(
    const APlayerController* LocalPC,
    const AActor* Instigator,
    const AActor* DamageCauser,
    const AActor* Target);

}
