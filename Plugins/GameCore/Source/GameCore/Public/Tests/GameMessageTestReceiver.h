// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"

#include "GameMessageTestReceiver.generated.h"

/**
 * @brief 消息总线测试载荷结构体
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FTestGameplayMessage {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameCore|Test")
    int32 Value = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameCore|Test")
    FString Text;
};

/**
 * @brief 异步消息动作测试接收器
 */
UCLASS()
class GAMECORE_API UGameMessageTestReceiver : public UObject {
    GENERATED_BODY()

public:
    /**
     * @brief 异步节点回调：累计接收次数并记录实际接收到的通道
     * @param ActualChannel 实际接收到消息的 GameplayTag 通道
     */
    UFUNCTION()
    void OnAsyncMessageReceived(FGameplayTag ActualChannel) {
        ++ReceivedCount;
        LastChannel = ActualChannel;
    }

    /**
     * @brief 加载状态变动回调：记录加载态与目标关卡
     * @param bInIsLoading 是否处于加载态
     * @param InTargetLevelName 目标关卡名称
     */
    UFUNCTION()
    void OnLoadingStateChanged(bool bInIsLoading, FName InTargetLevelName) {
        bLastIsLoading = bInIsLoading;
        LastTargetLevel = InTargetLevelName;
        ++LoadingChangeCount;
    }

    int32 ReceivedCount = 0;
    FGameplayTag LastChannel;

    bool bLastIsLoading = false;
    FName LastTargetLevel = NAME_None;
    int32 LoadingChangeCount = 0;
};

