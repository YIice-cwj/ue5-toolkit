// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"

#include "GameMessageTestReceiver.generated.h"

/**
 * @brief 消息总线测试载荷结构体
 */
USTRUCT(BlueprintType)
struct GAMEMESSAGE_API FTestGameplayMessage {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameMessage|Test")
    int32 Value = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameMessage|Test")
    FString Text;
};

/**
 * @brief 异步消息动作测试接收器
 */
UCLASS()
class GAMEMESSAGE_API UGameMessageTestReceiver : public UObject {
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

    int32 ReceivedCount = 0;
    FGameplayTag LastChannel;
};
