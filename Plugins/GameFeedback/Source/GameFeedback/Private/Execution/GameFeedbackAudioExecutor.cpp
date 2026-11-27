// Copyright Epic Games, Inc. All Rights Reserved.

#include "Execution/GameFeedbackAudioExecutor.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
#include "System/GameFeedbackDefinitionProvider.h"
#include "System/GameFeedbackInternalHelpers.h"

namespace GameFeedback::Execution {

/**
 * @brief 在命中位置播放空间打击音效（软引用未就绪时经 DefinitionProvider 去重异步预热并跳过本次播放）
 * @param World 当前世界
 * @param Signal 表现信号
 * @param Action 音效参数
 * @return 已同步派发播放返回 true；被门禁拦截、DS 短路或进入异步预热返回 false
 */
bool PlayImpactAudio(UWorld* World, const FGameFeedbackSignal& Signal, const FGameFeedbackAudioAction& Action) {
    if (!World || World->GetNetMode() == NM_DedicatedServer || Signal.Magnitude < Action.MinMagnitude || Action.SpatialImpactSound.IsNull()) {
        return false;
    }

    USoundBase* Sound = Action.SpatialImpactSound.Get();
    if (!Sound) {
        if (UGameFeedbackDefinitionProvider* Provider = UGameFeedbackDefinitionProvider::GetPtr(World)) {
            Provider->PreloadSoftAsset(Action.SpatialImpactSound.ToSoftObjectPath());
        }
        return false;
    }

    const FVector Location = GameFeedback::Internal::ResolveImpactLocation(Signal);
    UGameplayStatics::PlaySoundAtLocation(
        World,
        Sound,
        Location,
        FRotator::ZeroRotator,
        1.0f,
        1.0f,
        0.0f,
        Action.AttenuationSettings,
        Action.ConcurrencySettings
    );
    return true;
}

/**
 * @brief 为本地攻击者播放 2D 确认音（软引用未就绪时经 DefinitionProvider 去重异步预热并跳过本次播放）
 * @param World 当前世界
 * @param Signal 表现信号
 * @param Action 音效参数
 * @return 已同步派发播放返回 true；被门禁拦截、DS 短路或进入异步预热返回 false
 */
bool PlayAttackerConfirmation(UWorld* World, const FGameFeedbackSignal& Signal, const FGameFeedbackAudioAction& Action) {
    if (!World || World->GetNetMode() == NM_DedicatedServer || Signal.Magnitude < Action.MinMagnitude || Action.AttackerConfirmationSound.IsNull()) {
        return false;
    }

    USoundBase* Sound = Action.AttackerConfirmationSound.Get();
    if (!Sound) {
        if (UGameFeedbackDefinitionProvider* Provider = UGameFeedbackDefinitionProvider::GetPtr(World)) {
            Provider->PreloadSoftAsset(Action.AttackerConfirmationSound.ToSoftObjectPath());
        }
        return false;
    }

    UGameplayStatics::PlaySound2D(World, Sound, 1.0f, 1.0f, 0.0f, Action.ConcurrencySettings);
    return true;
}

}  // namespace GameFeedback::Execution
