// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/GameFeedbackWorldComponent.h"

#include "Asset/GameFeedbackDefinition.h"
#include "Bus/GameFeedbackChannels.h"
#include "Config/GameFeedbackSettings.h"
#include "Engine/World.h"
#include "Message/GameplayMessageSubsystem.h"
#include "System/GameFeedbackAudioExecutor.h"
#include "System/GameFeedbackDefinitionProvider.h"
#include "System/GameFeedbackFlashTracker.h"
#include "System/GameFeedbackVFXExecutor.h"

struct FGameFeedbackWorldComponentState {
    TArray<FGameplayMessageListenerHandle> Handles;
    TUniquePtr<FGameFeedbackFlashTracker> FlashTracker;
    uint64 LastNetworkFrame = MAX_uint64;
    TWeakObjectPtr<AActor> LastNetworkAttacker;
    TWeakObjectPtr<AActor> LastNetworkTarget;
    FVector LastNetworkImpact = FVector::ZeroVector;
};

/**
 * @brief 构造：装配闪白追踪状态
 */
UGameFeedbackWorldComponent::UGameFeedbackWorldComponent()
    : State(MakePimpl<FGameFeedbackWorldComponentState>())
{
    State->FlashTracker = MakeUnique<FGameFeedbackFlashTracker>();
    PrimaryComponentTick.bCanEverTick = false;
}

/**
 * @brief 析构：通过类型擦除释放内部状态
 */
UGameFeedbackWorldComponent::~UGameFeedbackWorldComponent() = default;

/**
 * @brief 开始播放：订阅 Play / Network / Clear 通道
 */
void UGameFeedbackWorldComponent::BeginPlay()
{
    Super::BeginPlay();

    UGameplayMessageSubsystem& Bus = UGameplayMessageSubsystem::Get(this);
    State->Handles.Add(Bus.RegisterListener<FGameFeedbackMessage>(TAG_Feedback_Channel_Play, this, &UGameFeedbackWorldComponent::OnPlay));
    State->Handles.Add(Bus.RegisterListener<FGameFeedbackNetworkMessage>(TAG_Feedback_Channel_Network, this, &UGameFeedbackWorldComponent::OnNetwork));
    State->Handles.Add(Bus.RegisterListener<FGameFeedbackClearMessage>(TAG_Feedback_Channel_Clear, this, &UGameFeedbackWorldComponent::OnClear));
}

/**
 * @brief 结束播放：还原 Overlay 并退订通道
 * @param EndPlayReason 结束原因
 */
void UGameFeedbackWorldComponent::EndPlay([[maybe_unused]] const EEndPlayReason::Type EndPlayReason)
{
    State->Handles.Empty();
    State->FlashTracker->Shutdown(GetWorld());
    Super::EndPlay(EndPlayReason);
}

/**
 * @brief 本地表现广播回调
 * @param Channel 广播通道
 * @param Message 表现载荷
 */
void UGameFeedbackWorldComponent::OnPlay([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackMessage& Message)
{
    HandleSignal(Message.Signal, Message.OverrideDefinition);
}

/**
 * @brief 网络回执回调（抑制或同帧重复时跳过）
 * @param Channel 广播通道
 * @param Message 网络载荷
 */
void UGameFeedbackWorldComponent::OnNetwork([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackNetworkMessage& Message)
{
    if (Message.bSuppressed) {
        return;
    }

    const FGameFeedbackSignal& Signal = Message.Signal;
    const FVector ImpactLocation = Signal.HitResult.ImpactPoint;
    const uint64 CurrentFrame = GFrameCounter;

    if (State->LastNetworkFrame == CurrentFrame
        && State->LastNetworkAttacker == Signal.Instigator
        && State->LastNetworkTarget == Signal.Target
        && State->LastNetworkImpact.Equals(ImpactLocation, KINDA_SMALL_NUMBER)) {
        return;
    }

    State->LastNetworkFrame = CurrentFrame;
    State->LastNetworkAttacker = Signal.Instigator;
    State->LastNetworkTarget = Signal.Target;
    State->LastNetworkImpact = ImpactLocation;
    HandleSignal(Signal, Message.ResolvedDefinition);
}

/**
 * @brief 清除指定实体表现回调
 * @param Channel 广播通道
 * @param Message 清除载荷
 */
void UGameFeedbackWorldComponent::OnClear([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackClearMessage& Message)
{
    AActor* Target = Message.Target.Get();
    if (Target) {
        State->FlashTracker->ClearActor(GetWorld(), Target);
    }
}

/**
 * @brief 解析资产并执行世界空间表现
 * @param Signal 表现信号
 * @param OverrideDefinition 覆盖资产；为空时按注册表解析
 */
void UGameFeedbackWorldComponent::HandleSignal(const FGameFeedbackSignal& Signal, const UGameFeedbackDefinition* OverrideDefinition)
{
    UWorld* World = GetWorld();
    if (!World) {
        return;
    }

    if (!GetDefault<UGameFeedbackSettings>()->bEnableFeedback) {
        return;
    }

    const UGameFeedbackDefinition* Definition = OverrideDefinition;
    if (!Definition) {
        UGameFeedbackDefinitionProvider* Provider = UGameFeedbackDefinitionProvider::GetPtr(this);
        if (!Provider) {
            return;
        }
        Definition = Provider->Resolve(Signal);
    }

    if (!Definition) {
        return;
    }

    State->FlashTracker->Execute(World, Signal.Target.Get(), Signal.HitResult, Definition->HitFlash, Signal.Magnitude);
    GameFeedback::Execution::SpawnVFX(World, Signal, Definition->VFX);
    GameFeedback::Execution::PlayImpactAudio(World, Signal, Definition->Audio);
}
