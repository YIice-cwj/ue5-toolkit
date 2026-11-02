// Copyright Epic Games, Inc. All Rights Reserved.

#include "Bus/GameFeedbackBlueprintLibrary.h"

#include "Asset/GameFeedbackDefinition.h"
#include "Bus/GameFeedbackChannels.h"
#include "Bus/GameFeedbackMessages.h"
#include "Config/GameFeedbackSettings.h"
#include "Message/GameplayMessageSubsystem.h"

namespace {

/**
 * @brief 以可选覆盖资产广播本地表现消息
 * @param WorldContextObject 世界上下文对象
 * @param Signal 纯表现信号
 * @param Definition 覆盖资产；为空则由监听者按注册表解析
 */
void BroadcastPlay(const UObject* WorldContextObject, const FGameFeedbackSignal& Signal, const UGameFeedbackDefinition* Definition)
{
    if (!GetDefault<UGameFeedbackSettings>()->bEnableFeedback) {
        return;
    }

    if (UGameplayMessageSubsystem* Bus = UGameplayMessageSubsystem::GetPtr(WorldContextObject)) {
        FGameFeedbackMessage Message;
        Message.Signal = Signal;
        Message.OverrideDefinition = const_cast<UGameFeedbackDefinition*>(Definition);
        Bus->BroadcastMessage(TAG_Feedback_Channel_Play, Message);
    }
}

}

/**
 * @brief 广播一次打击表现信号（由总线上的各能力监听者自行消费）
 * @param WorldContextObject 世界上下文对象
 * @param Signal 纯表现信号
 */
void UGameFeedbackBlueprintLibrary::BroadcastFeedback(const UObject* WorldContextObject, const FGameFeedbackSignal& Signal)
{
    BroadcastPlay(WorldContextObject, Signal, nullptr);
}

/**
 * @brief 以指定资产覆盖注册表匹配结果后广播一次表现信号
 * @param WorldContextObject 世界上下文对象
 * @param Signal 纯表现信号
 * @param Definition 指定的表现资产（仅本地监听者生效）
 */
void UGameFeedbackBlueprintLibrary::BroadcastFeedbackWithDefinition(
    const UObject* WorldContextObject,
    const FGameFeedbackSignal& Signal,
    const UGameFeedbackDefinition* Definition)
{
    BroadcastPlay(WorldContextObject, Signal, Definition);
}

/**
 * @brief 广播清除指定实体的表现状态
 * @param WorldContextObject 世界上下文对象
 * @param Target 待重置的目标实体
 */
void UGameFeedbackBlueprintLibrary::BroadcastClearFeedback(const UObject* WorldContextObject, AActor* Target)
{
    if (!GetDefault<UGameFeedbackSettings>()->bEnableFeedback || !Target) {
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
 */
void UGameFeedbackBlueprintLibrary::BroadcastNetworkFeedback(
    const UObject* WorldContextObject,
    const FGameFeedbackSignal& Signal,
    const UGameFeedbackDefinition* ResolvedDefinition,
    bool bSuppressed)
{
    if (!GetDefault<UGameFeedbackSettings>()->bEnableFeedback) {
        return;
    }

    if (UGameplayMessageSubsystem* Bus = UGameplayMessageSubsystem::GetPtr(WorldContextObject)) {
        FGameFeedbackNetworkMessage Message;
        Message.Signal = Signal;
        Message.ResolvedDefinition = const_cast<UGameFeedbackDefinition*>(ResolvedDefinition);
        Message.bSuppressed = bSuppressed;
        Bus->BroadcastMessage(TAG_Feedback_Channel_Network, Message);
    }
}
