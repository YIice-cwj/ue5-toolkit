/**
 * @file GameFeedbackVFXExecutor.h
 * @brief 粒子特效无状态执行器（软引用异步预热与降级）
 */

#pragma once

#include "CoreMinimal.h"
#include "Feedback/GameFeedbackActions.h"
#include "Feedback/GameFeedbackSignal.h"

class UWorld;

namespace GameFeedback::Execution {

/**
 * @brief 按显式信号方向计算粒子发射朝向
 * @param Signal 表现信号
 * @param OrientationMode 粒子朝向模式
 * @return 粒子世界旋转
 */
FRotator ResolveVFXRotation(const FGameFeedbackSignal& Signal, EGameFeedbackVFXOrientation OrientationMode);

/**
 * @brief 在世界中发射受击粒子特效（软引用未就绪时异步预热并跳过本次播放）
 * @param World 当前世界
 * @param Signal 表现信号
 * @param Action 粒子参数
 */
void SpawnVFX(UWorld* World, const FGameFeedbackSignal& Signal, const FGameFeedbackVFXAction& Action);

}
