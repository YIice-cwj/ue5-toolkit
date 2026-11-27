// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/GameFeedbackPlayerComponent.h"

#include "Asset/GameFeedbackDefinition.h"
#include "Bus/GameFeedbackChannels.h"
#include "Camera/PlayerCameraManager.h"
#include "Config/GameFeedbackSettings.h"
#include "Execution/GameFeedbackAudioExecutor.h"
#include "Feedback/GameFeedbackActions.h"
#include "Feedback/GameFeedbackPerspective.h"
#include "GameFramework/PlayerController.h"
#include "Haptics/HapticFeedbackEffect_Base.h"
#include "InputCoreTypes.h"
#include "Message/GameplayMessageSubsystem.h"
#include "System/GameFeedbackInternalHelpers.h"
#include "Tracker/GameFeedbackPredictionHistory.h"
#include "Tracker/GameFeedbackShakeTracker.h"

struct FGameFeedbackPlayerComponentState {
    TArray<FGameplayMessageListenerHandle> Handles;
    FGameFeedbackPredictionHistory PredictionHistory;
    FGameFeedbackShakeTracker ShakeTracker;
};

/**
 * @brief 构造：关闭 Tick 并按需装配内部状态（CDO 零堆分配）
 */
UGameFeedbackPlayerComponent::UGameFeedbackPlayerComponent() {
    PrimaryComponentTick.bCanEverTick = false;
    if (!HasAnyFlags(RF_ClassDefaultObject)) {
        State = MakePimpl<FGameFeedbackPlayerComponentState>();
    }
}

/**
 * @brief 析构：通过类型擦除释放内部状态
 */
UGameFeedbackPlayerComponent::~UGameFeedbackPlayerComponent() = default;

/**
 * @brief 开始播放：订阅 Play / Network 通道
 */
void UGameFeedbackPlayerComponent::BeginPlay() {
    Super::BeginPlay();

    EnsureState();
    State->Handles.Empty();

    UGameplayMessageSubsystem* Bus = UGameplayMessageSubsystem::GetPtr(this);
    if (!Bus) {
        return;
    }

    State->Handles.Add(Bus->RegisterListener<FGameFeedbackMessage>(TAG_Feedback_Channel_Play, this, &UGameFeedbackPlayerComponent::OnPlay));
    State->Handles.Add(Bus->RegisterListener<FGameFeedbackNetworkMessage>(TAG_Feedback_Channel_Network, this, &UGameFeedbackPlayerComponent::OnNetwork));
}

/**
 * @brief 结束播放：停止震动并退订通道
 * @param EndPlayReason 结束原因
 */
void UGameFeedbackPlayerComponent::EndPlay([[maybe_unused]] const EEndPlayReason::Type EndPlayReason) {
    if (State) {
        State->Handles.Empty();
        State->ShakeTracker.StopAll();
    }
    Super::EndPlay(EndPlayReason);
}

/**
 * @brief 消费一条本地预测指纹（供网络接收组件判定是否抑制回执）
 * @param Signal 网络还原后的表现信号
 * @return 命中并消费返回 true
 */
bool UGameFeedbackPlayerComponent::ConsumePrediction(const FGameFeedbackSignal& Signal) {
    if (!State) {
        return false;
    }
    return State->PredictionHistory.ConsumeMatch(Signal.PredictionEventId, Signal.Instigator, Signal.Target, FPlatformTime::Seconds());
}

/**
 * @brief 查询当前活跃镜头震动追踪数量（供测试与可观测性）
 * @return 活跃追踪数量
 */
int32 UGameFeedbackPlayerComponent::GetTrackedShakeCount() const {
    return State ? State->ShakeTracker.GetTrackedCount() : 0;
}

/**
 * @brief 确保非 CDO 实例已创建内部状态
 */
void UGameFeedbackPlayerComponent::EnsureState() {
    if (!State) {
        State = MakePimpl<FGameFeedbackPlayerComponentState>();
    }
}

/**
 * @brief 本地表现广播回调
 * @param Channel 广播通道
 * @param Message 表现载荷
 */
void UGameFeedbackPlayerComponent::OnPlay([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackMessage& Message) {
    HandleSignal(Message.Signal, Message.OverrideDefinition, false);
}

/**
 * @brief 网络回执回调（抑制标记为真或定向至其他本地分屏控制器时跳过）
 * @param Channel 广播通道
 * @param Message 网络载荷
 */
void UGameFeedbackPlayerComponent::OnNetwork([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackNetworkMessage& Message) {
    if (Message.bSuppressed) {
        return;
    }
    if (Message.ReceivingController.IsValid() && Message.ReceivingController.Get() != GetOwner()) {
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
void UGameFeedbackPlayerComponent::HandleSignal(const FGameFeedbackSignal& Signal, const UGameFeedbackDefinition* OverrideDefinition, bool bFromNetwork) {
    APlayerController* LocalPC = Cast<APlayerController>(GetOwner());
    UWorld* World = GetWorld();
    if (!State || !LocalPC || !World || !LocalPC->IsLocalController()) {
        return;
    }

    const UGameFeedbackSettings* Settings = GetDefault<UGameFeedbackSettings>();
    if (!Settings->bEnableFeedback) {
        return;
    }

    const UGameFeedbackDefinition* Definition = GameFeedback::Internal::ResolveDefinitionForSignal(this, Signal, OverrideDefinition);
    if (!Definition) {
        return;
    }

    if (!bFromNetwork && World->GetNetMode() == NM_Client) {
        State->PredictionHistory.Add(Signal.PredictionEventId, Signal.Instigator, Signal.Target, FPlatformTime::Seconds());
    }

    const EGameFeedbackPerspective Perspective = GameFeedback::ResolvePerspective(
        LocalPC, Signal.Instigator.Get(), Signal.DamageCauser.Get(), Signal.Target.Get());

    ExecuteCameraShake(*LocalPC, Perspective, Signal, Definition->CameraShake);
    ExecuteHaptics(*LocalPC, Perspective, Signal.Magnitude, Settings->GlobalHapticScale, Definition->Haptics);

    if (Perspective == EGameFeedbackPerspective::LocalAttacker) {
        GameFeedback::Execution::PlayAttackerConfirmation(World, Signal, Definition->Audio);
    }
}

/**
 * @brief 按观察视角执行镜头震动
 * @param LocalPC 本地玩家控制器
 * @param Perspective 观察视角
 * @param Signal 表现信号
 * @param Action 镜头震动参数
 */
void UGameFeedbackPlayerComponent::ExecuteCameraShake(
    APlayerController& LocalPC,
    EGameFeedbackPerspective Perspective,
    const FGameFeedbackSignal& Signal,
    const FGameFeedbackCameraShakeAction& Action) {
    if (Perspective == EGameFeedbackPerspective::Bystander || Signal.Magnitude < Action.MinMagnitude) {
        return;
    }

    APlayerCameraManager* CameraManager = LocalPC.PlayerCameraManager;
    if (!CameraManager) {
        return;
    }

    const bool bIsAttacker = Perspective == EGameFeedbackPerspective::LocalAttacker;
    const TSubclassOf<UCameraShakeBase> ShakeClass = bIsAttacker ? Action.AttackerShakeClass : Action.VictimShakeClass;
    const float ShakeScale = bIsAttacker ? Action.AttackerShakeScale : Action.VictimShakeScale;
    const FVector LocalImpactDirection = CameraManager->GetCameraRotation().UnrotateVector(Signal.ImpactDirection);

    State->ShakeTracker.Execute(
        CameraManager,
        ShakeClass,
        ShakeScale,
        LocalImpactDirection,
        Action.bOrientShakeToImpactDirection);
}

/**
 * @brief 按观察视角与无障碍倍率执行手柄触觉震动
 * @param LocalPC 本地玩家控制器
 * @param Perspective 观察视角
 * @param Magnitude 表现强度
 * @param GlobalHapticScale 项目全局触觉缩放倍率
 * @param Action 触觉震动参数
 */
void UGameFeedbackPlayerComponent::ExecuteHaptics(
    APlayerController& LocalPC,
    EGameFeedbackPerspective Perspective,
    float Magnitude,
    float GlobalHapticScale,
    const FGameFeedbackForceFeedbackAction& Action) {
    if (Perspective == EGameFeedbackPerspective::Bystander || Magnitude < Action.MinMagnitude || GlobalHapticScale <= 0.0f) {
        return;
    }

    UHapticFeedbackEffect_Base* HapticEffect = (Perspective == EGameFeedbackPerspective::LocalAttacker)
        ? Action.AttackerHaptic.Get()
        : Action.VictimHaptic.Get();
    if (HapticEffect) {
        LocalPC.PlayHapticEffect(HapticEffect, EControllerHand::AnyHand, GlobalHapticScale);
    }
}
