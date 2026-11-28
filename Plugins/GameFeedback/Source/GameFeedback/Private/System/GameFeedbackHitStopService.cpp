// Copyright Epic Games, Inc. All Rights Reserved.

#include "System/GameFeedbackHitStopService.h"

#include "Asset/GameFeedbackDefinition.h"
#include "Bus/GameFeedbackChannels.h"
#include "Config/GameFeedbackSettings.h"
#include "Feedback/GameFeedbackPerspective.h"
#include "GameFramework/PlayerController.h"
#include "Message/GameplayMessageSubsystem.h"
#include "System/GameFeedbackInternalHelpers.h"
#include "Time/TimeDilationSubsystem.h"
#include "Tracker/GameFeedbackHitStopLimiter.h"

struct FGameFeedbackHitStopServiceState {
    TArray<FGameplayMessageListenerHandle> Handles;
    FGameFeedbackHitStopLimiter Limiter;
};

/**
 * @brief 构造：仅在非 CDO 实例装配内部状态（CDO 零堆分配）
 */
UGameFeedbackHitStopService::UGameFeedbackHitStopService() {
    if (!HasAnyFlags(RF_ClassDefaultObject)) {
        State = MakePimpl<FGameFeedbackHitStopServiceState>();
    }
}

/**
 * @brief 析构：通过类型擦除释放内部状态
 */
UGameFeedbackHitStopService::~UGameFeedbackHitStopService() = default;

/**
 * @brief 仅在游戏世界创建（包含 Dedicated Server）
 * @param Outer 外层对象
 * @return 是否创建
 */
bool UGameFeedbackHitStopService::ShouldCreateSubsystem(UObject* Outer) const {
    if (!Super::ShouldCreateSubsystem(Outer)) {
        return false;
    }
    const UWorld* World = Outer ? Outer->GetWorld() : nullptr;
    return World && World->IsGameWorld();
}

/**
 * @brief 世界 BeginPlay：订阅 Play / Network / Clear 通道
 * @param InWorld 当前世界
 *
 * 无 GameInstance 的 Game 世界（引擎自动化测试、工具世界）不存在消息总线，此时跳过订阅。
 */
void UGameFeedbackHitStopService::OnWorldBeginPlay(UWorld& InWorld) {
    Super::OnWorldBeginPlay(InWorld);

    if (!State) {
        State = MakePimpl<FGameFeedbackHitStopServiceState>();
    }
    State->Handles.Empty();

    UGameplayMessageSubsystem* Bus = UGameplayMessageSubsystem::GetPtr(this);
    if (!Bus) {
        return;
    }

    State->Handles.Add(Bus->RegisterListener<FGameFeedbackMessage>(TAG_Feedback_Channel_Play, this, &UGameFeedbackHitStopService::OnPlay));
    State->Handles.Add(Bus->RegisterListener<FGameFeedbackNetworkMessage>(TAG_Feedback_Channel_Network, this, &UGameFeedbackHitStopService::OnNetwork));
    State->Handles.Add(Bus->RegisterListener<FGameFeedbackClearMessage>(TAG_Feedback_Channel_Clear, this, &UGameFeedbackHitStopService::OnClear));
}

/**
 * @brief 注销：退订全部通道并清空限流状态
 */
void UGameFeedbackHitStopService::Deinitialize() {
    if (State) {
        State->Handles.Empty();
        State->Limiter.Reset();
    }
    Super::Deinitialize();
}

/**
 * @brief 本地表现广播回调
 * @param Channel 广播通道
 * @param Message 表现载荷
 */
void UGameFeedbackHitStopService::OnPlay([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackMessage& Message) {
    ApplyHitStop(Message.Signal, Message.OverrideDefinition);
}

/**
 * @brief 网络回执回调（抑制标记为真时跳过）
 * @param Channel 广播通道
 * @param Message 网络载荷
 */
void UGameFeedbackHitStopService::OnNetwork([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackNetworkMessage& Message) {
    if (Message.bSuppressed) {
        return;
    }

    ApplyHitStop(Message.Signal, Message.ResolvedDefinition);
}

/**
 * @brief 清除指定实体顿帧状态回调
 * @param Channel 广播通道
 * @param Message 清除载荷
 */
void UGameFeedbackHitStopService::OnClear([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackClearMessage& Message) {
    AActor* Target = Message.Target.Get();
    if (!Target) {
        return;
    }

    if (UTimeDilationSubsystem* TimeSubsystem = UTimeDilationSubsystem::GetPtr(this)) {
        TimeSubsystem->ClearHitStop(Target);
    }
    if (State) {
        State->Limiter.ClearActor(Target);
    }
}

/**
 * @brief 应用一次本地顿帧（含视角分流与累积预算）
 * @param Signal 表现信号
 * @param OverrideDefinition 覆盖资产；为空时按注册表解析
 * @note 权威端（ListenServer / DedicatedServer）默认整段跳过；开启 bAllowAuthoritativeHitStop 后听服宿主会同时改变服务端模拟，当前未拆分纯表现层顿帧。
 */
void UGameFeedbackHitStopService::ApplyHitStop(const FGameFeedbackSignal& Signal, const UGameFeedbackDefinition* OverrideDefinition) {
    UWorld* World = GetWorld();
    if (!World || !State) {
        return;
    }

    const UGameFeedbackSettings* Settings = GetDefault<UGameFeedbackSettings>();
    if (!Settings->bEnableFeedback) {
        return;
    }

    const ENetMode NetMode = World->GetNetMode();
    if ((NetMode == NM_ListenServer || NetMode == NM_DedicatedServer) && !Settings->bAllowAuthoritativeHitStop) {
        return;
    }

    const UGameFeedbackDefinition* Definition = GameFeedback::Internal::ResolveDefinitionForSignal(this, Signal, OverrideDefinition);
    if (!Definition || !Definition->HitStop.bEnable) {
        return;
    }

    const FGameFeedbackHitStopAction& Action = Definition->HitStop;
    const double CurrentRealTime = FPlatformTime::Seconds();
    State->Limiter.PruneIfNeeded(CurrentRealTime);

    AActor* TargetActor = Signal.Target.Get();
    AActor* AttackerActor = Signal.Instigator.Get();
    if (!State->Limiter.PassCooldown(AttackerActor, TargetActor, Action, CurrentRealTime)) {
        return;
    }

    if (Signal.bLethal && Action.bGlobalFreezeOnLethal && Action.LethalGlobalFreezeDuration > 0.0f) {
        if (UTimeDilationSubsystem* TimeSubsystem = UTimeDilationSubsystem::GetPtr(this)) {
            TimeSubsystem->RequestGlobalDilation(
                ETimeDilationChannel::HitStop,
                Action.LethalGlobalFreezeDuration,
                Action.LethalGlobalFreezeDilation,
                Action.LethalGlobalFreezeBlendOut);
        }
    }

    ApplyActorHitStop(TargetActor, Action.VictimDuration, Action, CurrentRealTime);

    if (ShouldApplyAttackerHitStop(*World, Signal, AttackerActor, TargetActor)) {
        ApplyActorHitStop(AttackerActor, Action.AttackerDuration, Action, CurrentRealTime);
    }
}

/**
 * @brief 判断本次命中是否应为攻击者应用卡肉顿帧（排除自伤双重顿帧，并对称支持分屏多玩家）
 * @param World 当前世界
 * @param Signal 表现信号
 * @param AttackerActor 攻击者实体
 * @param TargetActor 受击者实体
 * @return 应应用攻击者顿帧返回 true
 */
bool UGameFeedbackHitStopService::ShouldApplyAttackerHitStop(
    const UWorld& World,
    const FGameFeedbackSignal& Signal,
    const AActor* AttackerActor,
    const AActor* TargetActor) const {
    if (!AttackerActor || AttackerActor == TargetActor) {
        return false;
    }

    bool bHasLocalController = false;
    for (FConstPlayerControllerIterator Iterator = World.GetPlayerControllerIterator(); Iterator; ++Iterator) {
        const APlayerController* LocalPC = Iterator->Get();
        if (!LocalPC || !LocalPC->IsLocalController()) {
            continue;
        }
        bHasLocalController = true;

        const EGameFeedbackPerspective Perspective = GameFeedback::ResolvePerspective(
            LocalPC, AttackerActor, Signal.DamageCauser.Get(), TargetActor);
        if (Perspective == EGameFeedbackPerspective::LocalAttacker) {
            return true;
        }
    }

    return !bHasLocalController && World.GetNetMode() == NM_Standalone;
}

/**
 * @brief 为单个实体申请累积预算内的顿帧并提交给时间膨胀子系统
 * @param Actor 目标实体
 * @param RequestedDuration 期望持续时长（秒）
 * @param Action 顿帧参数
 * @param CurrentRealTime 当前真实时间（秒）
 */
void UGameFeedbackHitStopService::ApplyActorHitStop(
    AActor* Actor,
    float RequestedDuration,
    const FGameFeedbackHitStopAction& Action,
    double CurrentRealTime) {
    if (!Actor || RequestedDuration <= 0.0f) {
        return;
    }

    const float AppliedDuration = State->Limiter.AcquireDuration(Actor, RequestedDuration, Action, CurrentRealTime);
    if (AppliedDuration > 0.0f) {
        if (UTimeDilationSubsystem* TimeSubsystem = UTimeDilationSubsystem::GetPtr(this)) {
            TimeSubsystem->RequestHitStop(Actor, AppliedDuration, Action.TimeDilation);
        }
    }
}
