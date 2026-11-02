/**
 * @file GameFeedbackMessages.h
 * @brief 打击感表现总线消息载荷
 */

#pragma once

#include "CoreMinimal.h"
#include "Feedback/GameFeedbackSignal.h"

#include "GameFeedbackMessages.generated.h"

class UGameFeedbackDefinition;

/**
 * @brief 本地表现广播载荷
 */
USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackMessage {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FGameFeedbackSignal Signal;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TObjectPtr<UGameFeedbackDefinition> OverrideDefinition = nullptr;
};

/**
 * @brief 网络回执广播载荷（携带本地预测抑制标记）
 */
USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackNetworkMessage {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FGameFeedbackSignal Signal;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TObjectPtr<UGameFeedbackDefinition> ResolvedDefinition = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    bool bSuppressed = false;
};

/**
 * @brief 清除指定实体表现状态的广播载荷
 */
USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackClearMessage {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TWeakObjectPtr<AActor> Target = nullptr;
};
