// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/GameFeedbackReceiverComponent.h"

#include "Bus/GameFeedbackBlueprintLibrary.h"
#include "Component/GameFeedbackPlayerComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "System/GameFeedbackDefinitionProvider.h"

/**
 * @brief 构造：关闭 Tick 并启用组件默认复制
 */
UGameFeedbackReceiverComponent::UGameFeedbackReceiverComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

/**
 * @brief 惰性确保目标 PlayerController 上存在接收组件（服务端创建后随组件复制同步到客户端）
 * @param PC 目标玩家控制器
 * @return 已存在或新建的接收组件指针；PC 为空时返回 nullptr
 */
UGameFeedbackReceiverComponent* UGameFeedbackReceiverComponent::EnsureOnPlayerController(APlayerController* PC)
{
    if (!PC) {
        return nullptr;
    }

    if (UGameFeedbackReceiverComponent* Existing = PC->FindComponentByClass<UGameFeedbackReceiverComponent>()) {
        return Existing;
    }

    UGameFeedbackReceiverComponent* Receiver = NewObject<UGameFeedbackReceiverComponent>(PC);
    PC->AddInstanceComponent(Receiver);
    Receiver->RegisterComponent();
    return Receiver;
}

/**
 * @brief 客户端接收实现：还原信号、判定预测抑制并回灌本地总线
 * @param Event 紧凑网络表现载荷
 */
void UGameFeedbackReceiverComponent::ClientReceiveFeedback_Implementation(const FGameFeedbackEventNet& Event)
{
    if (!GetWorld()) {
        return;
    }

    UGameFeedbackDefinitionProvider* Provider = UGameFeedbackDefinitionProvider::GetPtr(this);
    if (!Provider) {
        return;
    }

    const UGameFeedbackDefinition* ResolvedDefinition = Provider->ResolveById(Event.DefinitionId, Event.DefinitionMappingVersion);
    if (!ResolvedDefinition) {
        return;
    }

    const FGameFeedbackSignal Signal = FGameFeedbackEventNet::ToSignal(Event);
    bool bSuppressed = false;

    if (APlayerController* OwnerController = Cast<APlayerController>(GetOwner())) {
        if (UGameFeedbackPlayerComponent* PlayerComponent = OwnerController->FindComponentByClass<UGameFeedbackPlayerComponent>()) {
            bSuppressed = PlayerComponent->ConsumePrediction(Signal);
        }
    }

    UGameFeedbackBlueprintLibrary::BroadcastNetworkFeedback(this, Signal, ResolvedDefinition, bSuppressed);
}
