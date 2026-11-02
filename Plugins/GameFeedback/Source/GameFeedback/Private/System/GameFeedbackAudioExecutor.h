/**
 * @file GameFeedbackAudioExecutor.h
 * @brief 打击音效无状态执行器（空间音与攻击者确认音异步预热）
 */

#pragma once

#include "CoreMinimal.h"
#include "Feedback/GameFeedbackActions.h"
#include "Feedback/GameFeedbackSignal.h"

class UWorld;

namespace GameFeedback::Execution {

/**
 * @brief 在命中位置播放空间打击音效（软引用未就绪时异步预热并跳过本次播放）
 * @param World 当前世界
 * @param Signal 表现信号
 * @param Action 音效参数
 */
void PlayImpactAudio(UWorld* World, const FGameFeedbackSignal& Signal, const FGameFeedbackAudioAction& Action);

/**
 * @brief 为本地攻击者播放 2D 确认音（软引用未就绪时异步预热并跳过本次播放）
 * @param World 当前世界
 * @param Signal 表现信号
 * @param Action 音效参数
 */
void PlayAttackerConfirmation(UWorld* World, const FGameFeedbackSignal& Signal, const FGameFeedbackAudioAction& Action);

}
