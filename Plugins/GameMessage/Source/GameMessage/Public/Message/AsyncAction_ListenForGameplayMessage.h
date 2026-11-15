// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Message/GameplayMessageTypes.h"

#include "AsyncAction_ListenForGameplayMessage.generated.h"

class UGameplayMessageSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAsyncGameplayMessageDelegate, FGameplayTag, ActualChannel);

/**
 * @brief 异步监听 GameplayTag 消息的通用蓝图动作节点
 */
UCLASS()
class GAMEMESSAGE_API UAsyncAction_ListenForGameplayMessage : public UBlueprintAsyncActionBase {
    GENERATED_BODY()

public:
    /**
     * @brief 异步监听指定通道的消息广播（蓝图可直接使用）
     * @param WorldContextObject 关联的世界上下文对象
     * @param Channel 监听通道 GameplayTag
     * @param MatchRule 匹配模式（默认精确匹配）
     * @return 异步操作对象
     */
    UFUNCTION(BlueprintCallable, Category = "GameMessage|Message", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"))
    static UAsyncAction_ListenForGameplayMessage* ListenForGameplayMessages(
        UObject* WorldContextObject,
        FGameplayTag Channel,
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );

    /**
     * @brief 收到匹配消息时触发的蓝图执行引脚
     */
    UPROPERTY(BlueprintAssignable)
    FAsyncGameplayMessageDelegate OnMessageReceived;

    /**
     * @brief 节点激活回调：注册通道消息监听并将命中的消息转发至 OnMessageReceived 委托
     */
    void Activate() override;

    /**
     * @brief 终止异步操作并安全注销监听句柄
     */
    void SetReadyToDestroy() override;

    /**
     * @brief 手动终止并清理异步动作节点（供蓝图调用）
     */
    UFUNCTION(BlueprintCallable, Category = "GameMessage|Message", meta = (DisplayName = "Cancel Message Listener"))
    void Cancel();

    /**
     * @brief 提取上一次接收到的强类型载荷结构体（蓝图通配符引脚）
     * @param OutPayload 接收载荷的结构体变量引用
     * @return 成功提取且类型匹配返回 true，类型不匹配或无载荷返回 false
     */
    UFUNCTION(BlueprintCallable, CustomThunk, Category = "GameMessage|Message", meta = (CustomStructureParam = "OutPayload"))
    bool GetPayload(UPARAM(ref) int32& OutPayload);
    DECLARE_FUNCTION(execGetPayload);

    /**
     * @brief C++ 辅助方法：从上一次收到的消息中提取强类型载荷结构体
     * @tparam TMessageStructType 目标载荷结构体类型
     * @param OutPayload 接收载荷的结构体引用
     * @return 成功提取且类型匹配返回 true，类型不匹配或无载荷返回 false
     */
    template <typename TMessageStructType>
    bool GetPayloadTyped(TMessageStructType& OutPayload) const {
        const UScriptStruct* TargetStruct = TBaseStructure<TMessageStructType>::Get();
        if (TargetStruct == nullptr) {
            TargetStruct = TMessageStructType::StaticStruct();
        }
        if (ReceivedPayloadStructType.IsValid() && ReceivedPayloadStructType.Get() == TargetStruct && ReceivedPayloadData.Num() > 0) {
            TargetStruct->CopyScriptStruct(&OutPayload, ReceivedPayloadData.GetData());
            return true;
        }
        return false;
    }

private:
    void ClearReceivedPayload();

    TWeakObjectPtr<UObject> WorldContextObjectWeak;
    FGameplayTag TargetChannel;
    EGameplayMessageMatch TargetMatchRule = EGameplayMessageMatch::ExactMatch;
    FGameplayMessageListenerHandle ListenerHandle;
    TWeakObjectPtr<const UScriptStruct> ReceivedPayloadStructType;
    TArray<uint8> ReceivedPayloadData;
};
