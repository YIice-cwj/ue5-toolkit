// Copyright Epic Games, Inc. All Rights Reserved.

#include "System/GameFeedbackAudioExecutor.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace GameFeedback::Execution {

/**
 * @brief 在命中位置播放空间打击音效（软引用未就绪时异步预热并跳过本次播放）
 * @param World 当前世界
 * @param Signal 表现信号
 * @param Action 音效参数
 */
void PlayImpactAudio(UWorld* World, const FGameFeedbackSignal& Signal, const FGameFeedbackAudioAction& Action)
{
    if (!World || Signal.Magnitude < Action.MinMagnitude || Action.SpatialImpactSound.IsNull()) {
        return;
    }

    USoundBase* Sound = Action.SpatialImpactSound.Get();
    if (!Sound) {
        UAssetManager::GetStreamableManager().RequestAsyncLoad(
            Action.SpatialImpactSound.ToSoftObjectPath(),
            FStreamableDelegate(),
            FStreamableManager::DefaultAsyncLoadPriority,
            true);
        return;
    }

    const FVector Location = Signal.HitResult.ImpactPoint.IsNearlyZero()
        ? (Signal.Target.IsValid() ? Signal.Target->GetActorLocation() : FVector::ZeroVector)
        : Signal.HitResult.ImpactPoint;

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
}

/**
 * @brief 为本地攻击者播放 2D 确认音（软引用未就绪时异步预热并跳过本次播放）
 * @param World 当前世界
 * @param Signal 表现信号
 * @param Action 音效参数
 */
void PlayAttackerConfirmation(UWorld* World, const FGameFeedbackSignal& Signal, const FGameFeedbackAudioAction& Action)
{
    if (!World || Signal.Magnitude < Action.MinMagnitude || Action.AttackerConfirmationSound.IsNull()) {
        return;
    }

    USoundBase* Sound = Action.AttackerConfirmationSound.Get();
    if (!Sound) {
        UAssetManager::GetStreamableManager().RequestAsyncLoad(
            Action.AttackerConfirmationSound.ToSoftObjectPath(),
            FStreamableDelegate(),
            FStreamableManager::DefaultAsyncLoadPriority,
            true);
        return;
    }

    UGameplayStatics::PlaySound2D(World, Sound, 1.0f, 1.0f, 0.0f, Action.ConcurrencySettings);
}

}
