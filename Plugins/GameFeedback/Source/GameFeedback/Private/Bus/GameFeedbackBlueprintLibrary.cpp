// Copyright Epic Games, Inc. All Rights Reserved.

#include "Bus/GameFeedbackBlueprintLibrary.h"

#include "Asset/GameFeedbackDefinition.h"
#include "Bus/GameFeedbackChannels.h"
#include "Bus/GameFeedbackMessages.h"
#include "Config/GameFeedbackSettings.h"
#include "GameFramework/PlayerController.h"
#include "Message/GameplayMessageSubsystem.h"
#include "System/GameFeedbackDefinitionProvider.h"

/**
 * @brief 广播一次打击表现信号（未显式传入 Definition 时预解析一次供全部监听者复用）
 * @param WorldContextObject 世界上下文对象
 * @param Signal 纯表现信号
 * @param Definition 指定的表现资产（可选，为空时按注册表预解析）
 */
void UGameFeedbackBlueprintLibrary::BroadcastFeedback(
    const UObject* WorldContextObject,
    const FGameFeedbackSignal& Signal,
    const UGameFeedbackDefinition* Definition)
{
    if (!GetDefault<UGameFeedbackSettings>()->bEnableFeedback) {
        return;
    }

    UGameplayMessageSubsystem* Bus = UGameplayMessageSubsystem::GetPtr(WorldContextObject);
    if (!Bus) {
        return;
    }

    const UGameFeedbackDefinition* ResolvedDefinition = Definition;
    if (!ResolvedDefinition) {
        if (UGameFeedbackDefinitionProvider* Provider = UGameFeedbackDefinitionProvider::GetPtr(WorldContextObject)) {
            ResolvedDefinition = Provider->Resolve(Signal);
        }
    }

    FGameFeedbackMessage Message;
    Message.Signal = Signal;
    Message.OverrideDefinition = const_cast<UGameFeedbackDefinition*>(ResolvedDefinition);
    Bus->BroadcastMessage(TAG_Feedback_Channel_Play, Message);
}

/**
 * @brief 广播清除指定实体的表现状态（不受 bEnableFeedback 热关断开关阻断，始终放行清理复位）
 * @param WorldContextObject 世界上下文对象
 * @param Target 待重置的目标实体
 */
void UGameFeedbackBlueprintLibrary::BroadcastClearFeedback(const UObject* WorldContextObject, AActor* Target)
{
    if (!Target) {
        return;
    }

    if (UGameplayMessageSubsystem* Bus = UGameplayMessageSubsystem::GetPtr(WorldContextObject)) {
        FGameFeedbackClearMessage Message;
        Message.Target = Target;
        Bus->BroadcastMessage(TAG_Feedback_Channel_Clear, Message);
    }
}

/**
 * @brief 将携带服务端最终 Definition 的网络回执回灌本地总线
 * @param WorldContextObject 世界上下文对象
 * @param Signal 还原后的表现信号
 * @param ResolvedDefinition 服务端最终 Definition
 * @param bSuppressed 本地预测已播放而需要抑制重复表现
 * @param ReceivingController 可选定向接收该回执的本地玩家控制器（分屏下隔离玩家侧组件重复执行）
 */
void UGameFeedbackBlueprintLibrary::BroadcastNetworkFeedback(
    const UObject* WorldContextObject,
    const FGameFeedbackSignal& Signal,
    const UGameFeedbackDefinition* ResolvedDefinition,
    bool bSuppressed,
    APlayerController* ReceivingController)
{
    if (!GetDefault<UGameFeedbackSettings>()->bEnableFeedback) {
        return;
    }

    if (UGameplayMessageSubsystem* Bus = UGameplayMessageSubsystem::GetPtr(WorldContextObject)) {
        FGameFeedbackNetworkMessage Message;
        Message.Signal = Signal;
        Message.ResolvedDefinition = const_cast<UGameFeedbackDefinition*>(ResolvedDefinition);
        Message.bSuppressed = bSuppressed;
        Message.ReceivingController = ReceivingController;
        Bus->BroadcastMessage(TAG_Feedback_Channel_Network, Message);
    }
}
