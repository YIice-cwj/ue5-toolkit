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
#include "System/GameFeedbackInternalHelpers.h"

namespace {

/**
 * @brief 尝试获取远端控制器的有效观察坐标（避免把世界原点误判为空视点）
 * @param PC 目标玩家控制器
 * @param OutViewLocation 输出观察点世界坐标
 * @return 成功获取有效观察点返回 true
 */
bool TryGetControllerViewLocation(const APlayerController& PC, FVector& OutViewLocation) {
    if (const APawn* Pawn = PC.GetPawn()) {
        FRotator UnusedRotation;
        PC.GetPlayerViewPoint(OutViewLocation, UnusedRotation);
        if (OutViewLocation.IsNearlyZero() && !Pawn->GetActorLocation().IsNearlyZero()) {
            OutViewLocation = Pawn->GetActorLocation();
        }
        return true;
    }

    const AActor* ViewTarget = PC.GetViewTarget();
    if (ViewTarget && ViewTarget != &PC) {
        OutViewLocation = ViewTarget->GetActorLocation();
        return true;
    }

    FRotator UnusedRotation;
    PC.GetPlayerViewPoint(OutViewLocation, UnusedRotation);
    return !OutViewLocation.IsNearlyZero();
}

}  // namespace

struct FGameFeedbackNetworkDispatcherComponentState {
    TArray<FGameplayMessageListenerHandle> Handles;
    uint64 RateLimitFrame = MAX_uint64;
    int32 DispatchedThisFrame = 0;
};

/**
 * @brief 构造：关闭 Tick 并按需装配内部状态（CDO 零堆分配）
 */
UGameFeedbackNetworkDispatcherComponent::UGameFeedbackNetworkDispatcherComponent() {
    PrimaryComponentTick.bCanEverTick = false;
    if (!HasAnyFlags(RF_ClassDefaultObject)) {
        State = MakePimpl<FGameFeedbackNetworkDispatcherComponentState>();
    }
}

/**
 * @brief 析构：通过类型擦除释放内部状态
 */
UGameFeedbackNetworkDispatcherComponent::~UGameFeedbackNetworkDispatcherComponent() = default;

/**
 * @brief 开始播放：订阅 Play 通道
 */
void UGameFeedbackNetworkDispatcherComponent::BeginPlay() {
    Super::BeginPlay();

    EnsureState();
    State->Handles.Empty();

    UGameplayMessageSubsystem* Bus = UGameplayMessageSubsystem::GetPtr(this);
    if (!Bus) {
        return;
    }

    State->Handles.Add(Bus->RegisterListener<FGameFeedbackMessage>(TAG_Feedback_Channel_Play, this, &UGameFeedbackNetworkDispatcherComponent::OnPlay));
}

/**
 * @brief 结束播放：退订通道
 * @param EndPlayReason 结束原因
 */
void UGameFeedbackNetworkDispatcherComponent::EndPlay([[maybe_unused]] const EEndPlayReason::Type EndPlayReason) {
    if (State) {
        State->Handles.Empty();
    }
    Super::EndPlay(EndPlayReason);
}

/**
 * @brief 确保非 CDO 实例已创建内部状态
 */
void UGameFeedbackNetworkDispatcherComponent::EnsureState() {
    if (!State) {
        State = MakePimpl<FGameFeedbackNetworkDispatcherComponentState>();
    }
}

/**
 * @brief 本地表现广播回调（仅权威端响应）
 * @param Channel 广播通道
 * @param Message 表现载荷
 */
void UGameFeedbackNetworkDispatcherComponent::OnPlay([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackMessage& Message) {
    Dispatch(Message);
}

/**
 * @brief 按服务端最终 Definition 身份向视距内远端客户端派发紧凑报文
 * @param Message 本地表现消息（含可选 OverrideDefinition）
 */
void UGameFeedbackNetworkDispatcherComponent::Dispatch(const FGameFeedbackMessage& Message) {
    UWorld* World = GetWorld();
    if (!State || !World) {
        return;
    }

    const UGameFeedbackSettings* Settings = GetDefault<UGameFeedbackSettings>();
    if (!Settings->bEnableFeedback) {
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

    const UGameFeedbackDefinition* Definition = Message.OverrideDefinition
        ? Message.OverrideDefinition.Get()
        : Provider->Resolve(Signal);
    if (!Definition) {
        GAME_LOG_THROTTLE(1.0, Warning, TEXT("[NetworkDispatcher] 服务端未解析到最终 Definition，跳过网络派发。"));
        return;
    }

    const uint32 DefinitionId = Provider->GetDefinitionId(Definition);
    const uint32 DefinitionMappingVersion = Provider->GetDefinitionMappingVersion();
    if (DefinitionId == 0 || DefinitionMappingVersion == 0) {
        GAME_LOG_THROTTLE(1.0, Warning, TEXT("[NetworkDispatcher] Definition 未登记、存在 CRC 冲突或映射版本无效，跳过网络派发：%s"),
            *Definition->GetPathName());
        return;
    }

    FVector BroadcastOrigin = FVector::ZeroVector;
    const bool bHasBroadcastOrigin = GameFeedback::Internal::TryResolveImpactLocation(Signal, BroadcastOrigin);

    TArray<APlayerController*, TInlineAllocator<8>> VisibleControllers;
    CollectVisibleRemoteControllers(*World, BroadcastOrigin, bHasBroadcastOrigin, Settings->DefaultNetworkCullDistance, VisibleControllers);
    if (VisibleControllers.IsEmpty()) {
        return;
    }

    const uint64 CurrentFrame = GFrameCounter;
    if (State->RateLimitFrame != CurrentFrame) {
        State->RateLimitFrame = CurrentFrame;
        State->DispatchedThisFrame = 0;
    }

    if (Settings->MaxDispatchPerFrame > 0 && State->DispatchedThisFrame >= Settings->MaxDispatchPerFrame) {
        GAME_LOG_THROTTLE(1.0, Warning, TEXT("[NetworkDispatcher] 单帧网络派发达到上限 %d，本帧后续报文被丢弃。"), Settings->MaxDispatchPerFrame);
        return;
    }
    ++State->DispatchedThisFrame;

    const FGameFeedbackEventNet NetEvent = FGameFeedbackEventNet::FromSignal(Signal, DefinitionId, DefinitionMappingVersion);
    for (APlayerController* PC : VisibleControllers) {
        if (UGameFeedbackReceiverComponent* Receiver = UGameFeedbackReceiverComponent::EnsureOnPlayerController(PC)) {
            Receiver->ClientReceiveFeedback(NetEvent);
        }
    }
}

/**
 * @brief 筛选当前在视距裁剪范围内的所有远端玩家控制器
 * @param World 当前世界
 * @param BroadcastOrigin 表现源世界坐标
 * @param bHasBroadcastOrigin 是否存在有效表现源坐标
 * @param CullDistance 视距裁剪半径（<= 0 时不裁剪）
 * @param OutControllers 输出满足条件的远端控制器列表
 */
void UGameFeedbackNetworkDispatcherComponent::CollectVisibleRemoteControllers(
    const UWorld& World,
    const FVector& BroadcastOrigin,
    bool bHasBroadcastOrigin,
    float CullDistance,
    TArray<APlayerController*, TInlineAllocator<8>>& OutControllers) {
    const bool bEnableCulling = CullDistance > 0.0f && bHasBroadcastOrigin;
    const float CullDistSq = FMath::Square(CullDistance);

    for (FConstPlayerControllerIterator Iterator = World.GetPlayerControllerIterator(); Iterator; ++Iterator) {
        APlayerController* PC = Iterator->Get();
        if (!PC || PC->IsLocalController()) {
            continue;
        }

        FVector ViewLocation = FVector::ZeroVector;
        if (bEnableCulling && TryGetControllerViewLocation(*PC, ViewLocation)) {
            if (FVector::DistSquared(ViewLocation, BroadcastOrigin) > CullDistSq) {
                continue;
            }
        }

        OutControllers.Add(PC);
    }
}
