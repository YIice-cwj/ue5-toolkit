// Copyright Epic Games, Inc. All Rights Reserved.

#include "System/GameFeedbackHitStopService.h"

#include "Asset/GameFeedbackDefinition.h"
#include "Bus/GameFeedbackChannels.h"
#include "Config/GameFeedbackSettings.h"
#include "Feedback/GameFeedbackPerspective.h"
#include "GameFramework/PlayerController.h"
#include "Message/GameplayMessageSubsystem.h"
#include "System/GameFeedbackDefinitionProvider.h"
#include "System/GameFeedbackHitStopLimiter.h"
#include "Time/TimeDilationSubsystem.h"

struct FGameFeedbackHitStopServiceState {
    TArray<FGameplayMessageListenerHandle> Handles;
    TUniquePtr<FGameFeedbackHitStopLimiter> Limiter;
};

/**
 * @brief 构造：装配顿帧限流状态
 */
UGameFeedbackHitStopService::UGameFeedbackHitStopService()
    : State(MakePimpl<FGameFeedbackHitStopServiceState>())
{
    State->Limiter = MakeUnique<FGameFeedbackHitStopLimiter>();
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
bool UGameFeedbackHitStopService::ShouldCreateSubsystem(UObject* Outer) const
{
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
void UGameFeedbackHitStopService::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);

    UGameplayMessageSubsystem* Bus = UGameplayMessageSubsystem::GetPtr(this);
    if (!Bus) {
        return;
    }

    State->Handles.Add(Bus->RegisterListener<FGameFeedbackMessage>(TAG_Feedback_Channel_Play, this, &UGameFeedbackHitStopService::OnPlay));
    State->Handles.Add(Bus->RegisterListener<FGameFeedbackNetworkMessage>(TAG_Feedback_Channel_Network, this, &UGameFeedbackHitStopService::OnNetwork));
    State->Handles.Add(Bus->RegisterListener<FGameFeedbackClearMessage>(TAG_Feedback_Channel_Clear, this, &UGameFeedbackHitStopService::OnClear));
}

/**
 * @brief 注销：退订全部通道并释放状态
 */
void UGameFeedbackHitStopService::Deinitialize()
{
    State->Handles.Empty();
    Super::Deinitialize();
}

/**
 * @brief 安全获取当前世界的顿帧服务
 * @param WorldContextObject 世界上下文对象
 * @return 服务指针；未就绪时返回 nullptr
 */
UGameFeedbackHitStopService* UGameFeedbackHitStopService::GetPtr(const UObject* WorldContextObject)
{
    if (!WorldContextObject) {
        return nullptr;
    }
    const UWorld* World = WorldContextObject->GetWorld();
    return World ? World->GetSubsystem<UGameFeedbackHitStopService>() : nullptr;
}

/**
 * @brief 本地表现广播回调
 * @param Channel 广播通道
 * @param Message 表现载荷
 */
void UGameFeedbackHitStopService::OnPlay([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackMessage& Message)
{
    ApplyHitStop(Message.Signal, Message.OverrideDefinition);
}

/**
 * @brief 网络回执回调（抑制标记为真时跳过）
 * @param Channel 广播通道
 * @param Message 网络载荷
 */
void UGameFeedbackHitStopService::OnNetwork([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackNetworkMessage& Message)
{
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
void UGameFeedbackHitStopService::OnClear([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackClearMessage& Message)
{
    AActor* Target = Message.Target.Get();
    if (!Target) {
        return;
    }

    UTimeDilationSubsystem::Get(this).ClearHitStop(Target);
    State->Limiter->ClearActor(Target);
}

/**
 * @brief 应用一次本地顿帧（含视角分流与累积预算）
 * @param Signal 表现信号
 * @param OverrideDefinition 覆盖资产；为空时按注册表解析
 */
void UGameFeedbackHitStopService::ApplyHitStop(const FGameFeedbackSignal& Signal, const UGameFeedbackDefinition* OverrideDefinition)
{
    UWorld* World = GetWorld();
    if (!World) {
        return;
    }

    const UGameFeedbackSettings* Settings = GetDefault<UGameFeedbackSettings>();
    if ((World->GetNetMode() == NM_ListenServer || World->GetNetMode() == NM_DedicatedServer) && !Settings->bAllowAuthoritativeHitStop) {
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

    const FGameFeedbackHitStopAction& Action = Definition->HitStop;
    if (!Action.bEnable) {
        return;
    }

    const double CurrentRealTime = FPlatformTime::Seconds();
    State->Limiter->Prune(CurrentRealTime);

    AActor* TargetActor = Signal.Target.Get();
    AActor* AttackerActor = Signal.Instigator.Get();
    if (!State->Limiter->PassCooldown(AttackerActor, TargetActor, Action, CurrentRealTime)) {
        return;
    }

    if (Signal.bLethal && Action.bGlobalFreezeOnLethal) {
        UTimeDilationSubsystem::Get(this).RequestGlobalDilation(
            ETimeDilationChannel::HitStop, 0.05f, 0.1f, 0.02f);
    }

    if (TargetActor) {
        const float AppliedDuration = State->Limiter->AcquireDuration(TargetActor, Action.VictimDuration, Action, CurrentRealTime);
        if (AppliedDuration > 0.0f) {
            UTimeDilationSubsystem::Get(this).RequestHitStop(TargetActor, AppliedDuration, Action.TimeDilation);
        }
    }

    if (!AttackerActor) {
        return;
    }

    for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator) {
        APlayerController* LocalPC = Iterator->Get();
        if (!LocalPC || !LocalPC->IsLocalController()) {
            continue;
        }

        if (GameFeedback::ResolvePerspective(LocalPC, Signal.Instigator.Get(), Signal.DamageCauser.Get(), Signal.Target.Get()) != EGameFeedbackPerspective::LocalAttacker) {
            continue;
        }

        const float AppliedDuration = State->Limiter->AcquireDuration(AttackerActor, Action.AttackerDuration, Action, CurrentRealTime);
        if (AppliedDuration > 0.0f) {
            UTimeDilationSubsystem::Get(this).RequestHitStop(AttackerActor, AppliedDuration, Action.TimeDilation);
        }
        break;
    }
}
