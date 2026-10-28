// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "GameplayTagContainer.h"
#include "Message/GameplayMessageTypes.h"

#include "AsyncAction_ListenForGameplayMessage.generated.h"

class UGameplayMessageSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAsyncGameplayMessageDelegate, FGameplayTag, ActualChannel);

/**
 * @brief 异步监听 GameplayTag 消息的通用蓝图动作节点
 */
UCLASS()
class GAMECORE_API UAsyncAction_ListenForGameplayMessage : public UBlueprintAsyncActionBase {
    GENERATED_BODY()

public:
    /**
     * @brief 异步监听指定通道的消息广播（蓝图可直接使用）
     * @param WorldContextObject 关联的世界上下文对象
     * @param Channel 监听通道 GameplayTag
     * @param MatchRule 匹配模式（默认精确匹配）
     * @return 异步操作对象
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Message", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"))
    static UAsyncAction_ListenForGameplayMessage* ListenForGameplayMessages(
        UObject* WorldContextObject,
        FGameplayTag Channel,
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );

    /** 收到匹配消息时触发的执行引脚 */
    UPROPERTY(BlueprintAssignable)
    FAsyncGameplayMessageDelegate OnMessageReceived;

    /** 激活时注册消息监听并把命中消息转发到 OnMessageReceived；通道非法或子系统缺失则立即结束 */
    void Activate() override;

    /** 结束异步操作时注销监听句柄 */
    void SetReadyToDestroy() override;

private:
    TWeakObjectPtr<UObject> WorldContextObjectWeak;
    FGameplayTag TargetChannel;
    EGameplayMessageMatch TargetMatchRule = EGameplayMessageMatch::ExactMatch;
    FGameplayMessageListenerHandle ListenerHandle;
};

