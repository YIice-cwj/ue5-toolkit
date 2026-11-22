/**
 * @file UGameMessageTestReceiver.h
 * @brief GameMessage 自动化测试专用 UObject 接收器声明（仅供模块私有测试使用，不暴露于 Public）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Tests/FTestGameplayMessage.h"
#include "UObject/Object.h"

#include "UGameMessageTestReceiver.generated.h"

/**
 * @brief 异步消息动作与成员函数监听器测试接收器
 */
UCLASS(NotBlueprintable, Hidden)
class UGameMessageTestReceiver : public UObject {
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
     * @brief 成员函数监听回调（带通道与载荷）
     * @param ActualChannel 实际接收到消息的 GameplayTag 通道
     * @param Payload 强类型载荷引用
     */
    void OnTypedMessageReceived(FGameplayTag ActualChannel, const FTestGameplayMessage& Payload) {
        ++ReceivedCount;
        LastChannel = ActualChannel;
        LastPayloadValue = Payload.Value;
    }

    /**
     * @brief 成员函数监听回调（仅带载荷）
     * @param Payload 强类型载荷引用
     */
    void OnPayloadOnlyReceived(const FTestGameplayMessage& Payload) {
        ++ReceivedCount;
        LastPayloadValue = Payload.Value;
    }

    UPROPERTY()
    FTestGameplayMessage ExtractedPayloadSlot;

    int32 ReceivedCount = 0;
    int32 LastPayloadValue = 0;
    FGameplayTag LastChannel;
};
