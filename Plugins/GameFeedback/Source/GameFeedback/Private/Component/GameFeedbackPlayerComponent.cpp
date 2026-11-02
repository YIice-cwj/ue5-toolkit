// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/GameFeedbackPlayerComponent.h"

#include "Asset/GameFeedbackDefinition.h"
#include "Bus/GameFeedbackChannels.h"
#include "Camera/PlayerCameraManager.h"
#include "Config/GameFeedbackSettings.h"
#include "Feedback/GameFeedbackActions.h"
#include "Feedback/GameFeedbackPerspective.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Message/GameplayMessageSubsystem.h"
#include "System/GameFeedbackAudioExecutor.h"
#include "System/GameFeedbackDefinitionProvider.h"
#include "System/GameFeedbackPredictionHistory.h"
#include "System/GameFeedbackShakeTracker.h"

struct FGameFeedbackPlayerComponentState {
    TArray<FGameplayMessageListenerHandle> Handles;
    TUniquePtr<FGameFeedbackPredictionHistory> PredictionHistory;
    TUniquePtr<FGameFeedbackShakeTracker> ShakeTracker;
};

/**
 * @brief 构造：装配镜头震动与预测状态
 */
UGameFeedbackPlayerComponent::UGameFeedbackPlayerComponent()
    : State(MakePimpl<FGameFeedbackPlayerComponentState>())
{
    State->PredictionHistory = MakeUnique<FGameFeedbackPredictionHistory>();
    State->ShakeTracker = MakeUnique<FGameFeedbackShakeTracker>();
    PrimaryComponentTick.bCanEverTick = false;
}

/**
 * @brief 析构：通过类型擦除释放内部状态
 */
UGameFeedbackPlayerComponent::~UGameFeedbackPlayerComponent() = default;

/**
 * @brief 开始播放：订阅 Play / Network 通道
 */
void UGameFeedbackPlayerComponent::BeginPlay()
{
    Super::BeginPlay();

    UGameplayMessageSubsystem& Bus = UGameplayMessageSubsystem::Get(this);
    State->Handles.Add(Bus.RegisterListener<FGameFeedbackMessage>(TAG_Feedback_Channel_Play, this, &UGameFeedbackPlayerComponent::OnPlay));
    State->Handles.Add(Bus.RegisterListener<FGameFeedbackNetworkMessage>(TAG_Feedback_Channel_Network, this, &UGameFeedbackPlayerComponent::OnNetwork));
}

/**
 * @brief 结束播放：停止震动并退订通道
 * @param EndPlayReason 结束原因
 */
void UGameFeedbackPlayerComponent::EndPlay([[maybe_unused]] const EEndPlayReason::Type EndPlayReason)
{
    State->Handles.Empty();
    State->ShakeTracker->StopAll();
    Super::EndPlay(EndPlayReason);
}

/**
 * @brief 消费一条本地预测指纹（供网络接收组件判定是否抑制回执）
 * @param Signal 网络还原后的表现信号
 * @return 命中并消费返回 true
 */
bool UGameFeedbackPlayerComponent::ConsumePrediction(const FGameFeedbackSignal& Signal)
{
    return State->PredictionHistory->ConsumeMatch(Signal.PredictionEventId, Signal.Instigator, Signal.Target, FPlatformTime::Seconds());
}

/**
 * @brief 本地表现广播回调
 * @param Channel 广播通道
 * @param Message 表现载荷
 */
void UGameFeedbackPlayerComponent::OnPlay([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackMessage& Message)
{
    HandleSignal(Message.Signal, Message.OverrideDefinition, false);
}

/**
 * @brief 网络回执回调（抑制标记为真时跳过）
 * @param Channel 广播通道
 * @param Message 网络载荷
 */
void UGameFeedbackPlayerComponent::OnNetwork([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackNetworkMessage& Message)
{
    if (Message.bSuppressed) {
        return;
    }

    HandleSignal(Message.Signal, Message.ResolvedDefinition, true);
}

/**
 * @brief 解析视角并执行玩家侧表现
 * @param Signal 表现信号
 * @param OverrideDefinition 覆盖资产；为空时按注册表解析
 * @param bFromNetwork 是否来自网络回执
 */
void UGameFeedbackPlayerComponent::HandleSignal(const FGameFeedbackSignal& Signal, const UGameFeedbackDefinition* OverrideDefinition, bool bFromNetwork)
{
    APlayerController* LocalPC = Cast<APlayerController>(GetOwner());
    UWorld* World = GetWorld();
    if (!LocalPC || !World || !LocalPC->IsLocalController()) {
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

    if (!bFromNetwork && World->GetNetMode() == NM_Client) {
        State->PredictionHistory->Add(Signal.PredictionEventId, Signal.Instigator, Signal.Target, FPlatformTime::Seconds());
    }

    const EGameFeedbackPerspective Perspective = GameFeedback::ResolvePerspective(
        LocalPC, Signal.Instigator.Get(), Signal.DamageCauser.Get(), Signal.Target.Get());

    if (Perspective != EGameFeedbackPerspective::Bystander && Signal.Magnitude >= Definition->CameraShake.MinMagnitude) {
        if (APlayerCameraManager* CameraManager = LocalPC->PlayerCameraManager) {
            const FRotator CameraRotation = CameraManager->GetCameraRotation();
            const FVector LocalImpactDirection = CameraRotation.UnrotateVector(Signal.ImpactDirection);
            State->ShakeTracker->Execute(
                CameraManager,
                Perspective == EGameFeedbackPerspective::LocalAttacker ? Definition->CameraShake.AttackerShakeClass : Definition->CameraShake.VictimShakeClass,
                Perspective == EGameFeedbackPerspective::LocalAttacker ? Definition->CameraShake.AttackerShakeScale : Definition->CameraShake.VictimShakeScale,
                LocalImpactDirection,
                Definition->CameraShake.bOrientShakeToImpactDirection);
        }
    }

    if (Perspective != EGameFeedbackPerspective::Bystander && Signal.Magnitude >= Definition->Haptics.MinMagnitude) {
        UHapticFeedbackEffect_Base* HapticEffect = (Perspective == EGameFeedbackPerspective::LocalAttacker ? Definition->Haptics.AttackerHaptic : Definition->Haptics.VictimHaptic);
        if (HapticEffect) {
            const float Scale = GetDefault<UGameFeedbackSettings>()->GlobalHapticScale;
            if (Scale > 0.0f) {
                LocalPC->PlayHapticEffect(HapticEffect, EControllerHand::AnyHand, Scale);
            }
        }
    }

    if (Perspective == EGameFeedbackPerspective::LocalAttacker) {
        GameFeedback::Execution::PlayAttackerConfirmation(World, Signal, Definition->Audio);
    }
}
