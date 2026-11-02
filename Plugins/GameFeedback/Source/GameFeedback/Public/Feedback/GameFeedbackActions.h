/**
 * @file GameFeedbackActions.h
 * @brief 打击感表现原子动作参数聚合定义
 */

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "Haptics/HapticFeedbackEffect_Base.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"

#include "GameFeedbackActions.generated.h"

/**
 * @brief 粒子特效发射朝向模式
 */
UENUM(BlueprintType)
enum class EGameFeedbackVFXOrientation : uint8 {
    AlignToNormal,
    AlignToImpactDirection,
    OppositeToTraceDirection,
    AlignToReflectedNormal
};

/**
 * @brief 多部件网格体受击闪白目标策略
 */
UENUM(BlueprintType)
enum class EGameFeedbackFlashTargetMode : uint8 {
    HitComponentOnly,
    AllSkeletalMeshes,
    TaggedComponentsOnly
};

/**
 * @brief 打击顿帧原子参数
 */
USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackHitStopAction {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitStop")
    bool bEnable = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitStop", meta = (ClampMin = "0.0", UIMin = "0.0"))
    float VictimDuration = 0.08f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitStop", meta = (ClampMin = "0.0", UIMin = "0.0"))
    float AttackerDuration = 0.04f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitStop", meta = (ClampMin = "0.0001", ClampMax = "1.0"))
    float TimeDilation = 0.001f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitStop", meta = (ClampMin = "0.01"))
    float CooldownInterval = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitStop", meta = (ClampMin = "0.05"))
    float MaxCumulativeDuration = 0.20f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitStop", meta = (ClampMin = "0.1"))
    float ResetWindow = 0.50f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitStop")
    bool bGlobalFreezeOnLethal = false;
};

/**
 * @brief 镜头震动原子参数
 */
USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackCameraShakeAction {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CameraShake")
    TSubclassOf<UCameraShakeBase> AttackerShakeClass;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CameraShake")
    float AttackerShakeScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CameraShake")
    TSubclassOf<UCameraShakeBase> VictimShakeClass;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CameraShake")
    float VictimShakeScale = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CameraShake", meta = (ClampMin = "0.0"))
    float MinMagnitude = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CameraShake")
    bool bOrientShakeToImpactDirection = true;
};

/**
 * @brief 材质受击闪白原子参数
 */
USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackHitFlashAction {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitFlash")
    bool bEnable = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitFlash")
    EGameFeedbackFlashTargetMode TargetMode = EGameFeedbackFlashTargetMode::AllSkeletalMeshes;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitFlash", meta = (EditCondition = "TargetMode == EGameFeedbackFlashTargetMode::TaggedComponentsOnly"))
    FName ComponentFilterTag = TEXT("Flashable");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitFlash")
    TObjectPtr<UMaterialInterface> OverlayMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitFlash", meta = (ClampMin = "0.01"))
    float Duration = 0.10f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitFlash", meta = (ClampMin = "0.0"))
    float MinMagnitude = 3.0f;
};

/**
 * @brief 粒子特效原子参数
 */
USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackVFXAction {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VFX")
    TSoftObjectPtr<UNiagaraSystem> NiagaraSystem;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VFX")
    bool bAttachToHitComponent = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VFX")
    EGameFeedbackVFXOrientation OrientationMode = EGameFeedbackVFXOrientation::AlignToNormal;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VFX", meta = (ClampMin = "0.0"))
    float MinMagnitude = 1.0f;
};

/**
 * @brief 音效原子参数
 */
USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackAudioAction {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
    TSoftObjectPtr<USoundBase> SpatialImpactSound;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
    TObjectPtr<USoundAttenuation> AttenuationSettings = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
    TObjectPtr<USoundConcurrency> ConcurrencySettings = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
    TSoftObjectPtr<USoundBase> AttackerConfirmationSound;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.0"))
    float MinMagnitude = 1.0f;
};

/**
 * @brief 手柄触觉震动原子参数
 */
USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackForceFeedbackAction {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Haptics")
    TObjectPtr<UHapticFeedbackEffect_Base> AttackerHaptic = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Haptics")
    TObjectPtr<UHapticFeedbackEffect_Base> VictimHaptic = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Haptics", meta = (ClampMin = "0.0"))
    float MinMagnitude = 5.0f;
};
