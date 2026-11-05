// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/GameFeedbackNetworkDispatcherComponent.h"

#include "Asset/GameFeedbackDefinition.h"
#include "Bus/GameFeedbackChannels.h"
#include "Config/GameFeedbackSettings.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Logging/GameLogCategories.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Network/GameFeedbackEventNet.h"
#include "Network/GameFeedbackReceiverComponent.h"
#include "System/GameFeedbackDefinitionProvider.h"

struct FGameFeedbackNetworkDispatcherComponentState {
    TArray<FGameplayMessageListenerHandle> Handles;
    uint64 RateLimitFrame = MAX_uint64;
    int32 DispatchedThisFrame = 0;
};

/**
 * @brief 构造：装配派发限流状态
 */
UGameFeedbackNetworkDispatcherComponent::UGameFeedbackNetworkDispatcherComponent()
    : State(MakePimpl<FGameFeedbackNetworkDispatcherComponentState>())
{
    PrimaryComponentTick.bCanEverTick = false;
}

/**
 * @brief 析构：通过类型擦除释放内部状态
 */
UGameFeedbackNetworkDispatcherComponent::~UGameFeedbackNetworkDispatcherComponent() = default;

/**
 * @brief 开始播放：订阅 Play 通道
 */
void UGameFeedbackNetworkDispatcherComponent::BeginPlay()
{
    Super::BeginPlay();

    UGameplayMessageSubsystem& Bus = UGameplayMessageSubsystem::Get(this);
    State->Handles.Add(Bus.RegisterListener<FGameFeedbackMessage>(TAG_Feedback_Channel_Play, this, &UGameFeedbackNetworkDispatcherComponent::OnPlay));
}

/**
 * @brief 结束播放：退订通道
 * @param EndPlayReason 结束原因
 */
void UGameFeedbackNetworkDispatcherComponent::EndPlay([[maybe_unused]] const EEndPlayReason::Type EndPlayReason)
{
    State->Handles.Empty();
    Super::EndPlay(EndPlayReason);
}

/**
 * @brief 本地表现广播回调（仅权威端响应）
 * @param Channel 广播通道
 * @param Message 表现载荷
 */
void UGameFeedbackNetworkDispatcherComponent::OnPlay([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackMessage& Message)
{
    Dispatch(Message);
}

/**
 * @brief 按服务端最终 Definition 身份向视距内远端客户端派发紧凑报文
 * @param Message 本地表现消息（含可选 OverrideDefinition）
 */
void UGameFeedbackNetworkDispatcherComponent::Dispatch(const FGameFeedbackMessage& Message)
{
    UWorld* World = GetWorld();
    if (!World) {
        return;
    }

    if (!GetDefault<UGameFeedbackSettings>()->bEnableFeedback) {
        return;
    }

    const ENetMode NetMode = World->GetNetMode();
    if (NetMode != NM_ListenServer && NetMode != NM_DedicatedServer) {
        return;
    }

    const FGameFeedbackSignal& Signal = Message.Signal;
    UGameFeedbackDefinitionProvider* Provider = UGameFeedbackDefinitionProvider::GetPtr(this);
    if (!Provider) {
        return;
    }

    const UGameFeedbackDefinition* Definition = Message.OverrideDefinition;
    if (!Definition) {
        Definition = Provider->Resolve(Signal);
    }
    if (!Definition) {
        GAME_LOG_THROTTLE(1.0, Warning, TEXT("【GameFeedback】服务端未解析到最终 Definition，跳过网络派发。"));
        return;
    }

    const uint32 DefinitionId = Provider->GetDefinitionId(Definition);
    const uint32 DefinitionMappingVersion = Provider->GetDefinitionMappingVersion();
    if (DefinitionId == 0 || DefinitionMappingVersion == 0) {
        GAME_LOG_THROTTLE(1.0, Warning, TEXT("【GameFeedback】Definition 未登记、存在 CRC 冲突或映射版本无效，跳过网络派发：%s"),
            *Definition->GetPathName());
        return;
    }

    const UGameFeedbackSettings* Settings = GetDefault<UGameFeedbackSettings>();
    const uint64 CurrentFrame = GFrameCounter;
    if (State->RateLimitFrame != CurrentFrame) {
        State->RateLimitFrame = CurrentFrame;
        State->DispatchedThisFrame = 0;
    }

    if (Settings->MaxDispatchPerFrame > 0 && State->DispatchedThisFrame >= Settings->MaxDispatchPerFrame) {
        GAME_LOG_THROTTLE(1.0, Warning, TEXT("【GameFeedback】单帧网络派发达到上限 %d，本帧后续报文被丢弃。"), Settings->MaxDispatchPerFrame);
        return;
    }
    ++State->DispatchedThisFrame;

    const float CullDistance = Settings->DefaultNetworkCullDistance;
    const bool bEnableCulling = CullDistance > 0.0f;
    const float CullDistSq = FMath::Square(CullDistance);
    FVector BroadcastOrigin = FVector::ZeroVector;
    bool bHasBroadcastOrigin = false;
    if (Signal.HitResult.bBlockingHit || !Signal.HitResult.ImpactPoint.IsNearlyZero()) {
        BroadcastOrigin = Signal.HitResult.ImpactPoint;
        bHasBroadcastOrigin = true;
    } else if (Signal.Target.IsValid()) {
        BroadcastOrigin = Signal.Target->GetActorLocation();
        bHasBroadcastOrigin = true;
    }

    const FGameFeedbackEventNet NetEvent = FGameFeedbackEventNet::FromSignal(Signal, DefinitionId, DefinitionMappingVersion);

    for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator) {
        APlayerController* PC = Iterator->Get();
        if (!PC || PC->IsLocalController()) {
            continue;
        }

        if (bEnableCulling && bHasBroadcastOrigin) {
            FVector ViewLocation = FVector::ZeroVector;
            FRotator ViewRotation = FRotator::ZeroRotator;
            PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
            if (ViewLocation.IsNearlyZero()) {
                if (const APawn* Pawn = PC->GetPawn()) {
                    ViewLocation = Pawn->GetActorLocation();
                }
            }
            if (!ViewLocation.IsNearlyZero() && FVector::DistSquared(ViewLocation, BroadcastOrigin) > CullDistSq) {
                continue;
            }
        }

        if (UGameFeedbackReceiverComponent* Receiver = UGameFeedbackReceiverComponent::EnsureOnPlayerController(PC)) {
            Receiver->ClientReceiveFeedback(NetEvent);
        }
    }
}
