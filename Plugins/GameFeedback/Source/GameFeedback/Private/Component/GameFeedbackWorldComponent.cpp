// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/GameFeedbackWorldComponent.h"

#include "Asset/GameFeedbackDefinition.h"
#include "Bus/GameFeedbackChannels.h"
#include "Config/GameFeedbackSettings.h"
#include "Engine/World.h"
#include "Execution/GameFeedbackAudioExecutor.h"
#include "Execution/GameFeedbackVFXExecutor.h"
#include "Message/GameplayMessageSubsystem.h"
#include "System/GameFeedbackInternalHelpers.h"
#include "Tracker/GameFeedbackFlashTracker.h"
#include "UObject/ObjectKey.h"

namespace {

struct FNetworkDedupEntry {
    TObjectKey<AActor> Attacker;
    TObjectKey<AActor> Target;
    TObjectKey<const UGameFeedbackDefinition> Definition;
    FVector ImpactLocation = FVector::ZeroVector;
};

}  // namespace

struct FGameFeedbackWorldComponentState {
    TArray<FGameplayMessageListenerHandle> Handles;
    FGameFeedbackFlashTracker FlashTracker;
    uint64 LastNetworkFrame = MAX_uint64;
    TArray<FNetworkDedupEntry, TInlineAllocator<8>> FrameNetworkEntries;
};

/**
 * @brief 构造：关闭 Tick 并按需装配内部状态（CDO 零堆分配）
 */
UGameFeedbackWorldComponent::UGameFeedbackWorldComponent() {
    PrimaryComponentTick.bCanEverTick = false;
    if (!HasAnyFlags(RF_ClassDefaultObject)) {
        State = MakePimpl<FGameFeedbackWorldComponentState>();
    }
}

/**
 * @brief 析构：通过类型擦除释放内部状态
 */
UGameFeedbackWorldComponent::~UGameFeedbackWorldComponent() = default;

/**
 * @brief 开始播放：订阅 Play / Network / Clear 通道
 */
void UGameFeedbackWorldComponent::BeginPlay() {
    Super::BeginPlay();

    EnsureState();
    State->Handles.Empty();

    UGameplayMessageSubsystem* Bus = UGameplayMessageSubsystem::GetPtr(this);
    if (!Bus) {
        return;
    }

    State->Handles.Add(Bus->RegisterListener<FGameFeedbackMessage>(TAG_Feedback_Channel_Play, this, &UGameFeedbackWorldComponent::OnPlay));
    State->Handles.Add(Bus->RegisterListener<FGameFeedbackNetworkMessage>(TAG_Feedback_Channel_Network, this, &UGameFeedbackWorldComponent::OnNetwork));
    State->Handles.Add(Bus->RegisterListener<FGameFeedbackClearMessage>(TAG_Feedback_Channel_Clear, this, &UGameFeedbackWorldComponent::OnClear));
}

/**
 * @brief 结束播放：还原 Overlay 并退订通道
 * @param EndPlayReason 结束原因
 */
void UGameFeedbackWorldComponent::EndPlay([[maybe_unused]] const EEndPlayReason::Type EndPlayReason) {
    if (State) {
        State->Handles.Empty();
        State->FrameNetworkEntries.Empty();
        State->FlashTracker.Shutdown(GetWorld());
    }
    Super::EndPlay(EndPlayReason);
}

/**
 * @brief 确保非 CDO 实例已创建内部状态
 */
void UGameFeedbackWorldComponent::EnsureState() {
    if (!State) {
        State = MakePimpl<FGameFeedbackWorldComponentState>();
    }
}

/**
 * @brief 本地表现广播回调
 * @param Channel 广播通道
 * @param Message 表现载荷
 */
void UGameFeedbackWorldComponent::OnPlay([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackMessage& Message) {
    HandleSignal(Message.Signal, Message.OverrideDefinition);
}

/**
 * @brief 网络回执回调（抑制或同帧重复时跳过，支持分屏交错到达去重）
 * @param Channel 广播通道
 * @param Message 网络载荷
 */
void UGameFeedbackWorldComponent::OnNetwork([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackNetworkMessage& Message) {
    if (Message.bSuppressed || !State) {
        return;
    }

    const FGameFeedbackSignal& Signal = Message.Signal;
    const FVector ImpactLocation = Signal.HitResult.ImpactPoint;
    const uint64 CurrentFrame = GFrameCounter;

    if (State->LastNetworkFrame != CurrentFrame) {
        State->LastNetworkFrame = CurrentFrame;
        State->FrameNetworkEntries.Reset();
    }

    const TObjectKey<AActor> AttackerKey(Signal.Instigator.Get());
    const TObjectKey<AActor> TargetKey(Signal.Target.Get());
    const TObjectKey<const UGameFeedbackDefinition> DefKey(Message.ResolvedDefinition.Get());

    for (const FNetworkDedupEntry& Existing : State->FrameNetworkEntries) {
        if (Existing.Attacker == AttackerKey
            && Existing.Target == TargetKey
            && Existing.Definition == DefKey
            && Existing.ImpactLocation.Equals(ImpactLocation, KINDA_SMALL_NUMBER)) {
            return;
        }
    }

    State->FrameNetworkEntries.Add(FNetworkDedupEntry{ AttackerKey, TargetKey, DefKey, ImpactLocation });
    HandleSignal(Signal, Message.ResolvedDefinition);
}

/**
 * @brief 清除指定实体表现回调
 * @param Channel 广播通道
 * @param Message 清除载荷
 */
void UGameFeedbackWorldComponent::OnClear([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackClearMessage& Message) {
    if (!State) {
        return;
    }

    if (AActor* Target = Message.Target.Get()) {
        State->FlashTracker.ClearActor(GetWorld(), Target);
    }
}

/**
 * @brief 解析资产并执行世界空间表现
 * @param Signal 表现信号
 * @param OverrideDefinition 覆盖资产；为空时按注册表解析
 */
void UGameFeedbackWorldComponent::HandleSignal(const FGameFeedbackSignal& Signal, const UGameFeedbackDefinition* OverrideDefinition) {
    UWorld* World = GetWorld();
    if (!State || !World) {
        return;
    }

    if (!GetDefault<UGameFeedbackSettings>()->bEnableFeedback) {
        return;
    }

    const UGameFeedbackDefinition* Definition = GameFeedback::Internal::ResolveDefinitionForSignal(this, Signal, OverrideDefinition);
    if (!Definition) {
        return;
    }

    State->FlashTracker.Execute(World, Signal.Target.Get(), Signal.HitResult, Definition->HitFlash, Signal.Magnitude);
    GameFeedback::Execution::SpawnVFX(World, Signal, Definition->VFX);
    GameFeedback::Execution::PlayImpactAudio(World, Signal, Definition->Audio);
}
