/**
 * @file GameFeedbackSignal.h
 * @brief 打击感表现纯信号载荷与本地视角枚举
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"

#include "GameFeedbackSignal.generated.h"

GAMEFEEDBACK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Feedback_State_Lethal);

/**
 * @brief 本地客户端观察视角枚举
 */
UENUM(BlueprintType)
enum class EGameFeedbackPerspective : uint8 {
    LocalAttacker,
    LocalVictim,
    Bystander
};

/**
 * @brief 纯表现通用信号载荷（与战斗规则层彻底解耦）
 */
USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackSignal {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TWeakObjectPtr<AActor> Instigator = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TWeakObjectPtr<AActor> DamageCauser = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TWeakObjectPtr<AActor> Target = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FHitResult HitResult;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FVector ImpactDirection = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FVector TraceDirection = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FGameplayTag PrimaryChannel;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FGameplayTagContainer FeedbackTags;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    float Magnitude = 0.0f;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    bool bLethal = false;

    UPROPERTY()
    uint16 PredictionEventId = 0;
};
