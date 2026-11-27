// Copyright Epic Games, Inc. All Rights Reserved.

#include "System/GameFeedbackRuntimeSubsystem.h"

#include "Component/GameFeedbackNetworkDispatcherComponent.h"
#include "Component/GameFeedbackPlayerComponent.h"
#include "Component/GameFeedbackWorldComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Logging/GameLogCategories.h"
#include "Network/GameFeedbackReceiverComponent.h"
#include "System/GameFeedbackInternalHelpers.h"
#include "TimerManager.h"

namespace {
constexpr float EnsurePollIntervalSeconds = 0.25f;
}

/**
 * @brief 仅保留游戏世界
 * @param Outer 外层对象
 * @return 是否创建
 */
bool UGameFeedbackRuntimeSubsystem::ShouldCreateSubsystem(UObject* Outer) const {
    if (!Super::ShouldCreateSubsystem(Outer)) {
        return false;
    }
    const UWorld* World = Outer ? Outer->GetWorld() : nullptr;
    return World && World->IsGameWorld();
}

/**
 * @brief 世界 BeginPlay：装配现有组件并注册服务端登录与本地分屏加入事件
 * @param InWorld 当前世界
 */
void UGameFeedbackRuntimeSubsystem::OnWorldBeginPlay(UWorld& InWorld) {
    Super::OnWorldBeginPlay(InWorld);

    const bool bAllReady = EnsureComponents(InWorld);

    const ENetMode NetMode = InWorld.GetNetMode();
    if (NetMode == NM_ListenServer || NetMode == NM_DedicatedServer) {
        PostLoginDelegateHandle = FGameModeEvents::GameModePostLoginEvent.AddUObject(
            this, &UGameFeedbackRuntimeSubsystem::OnPostLogin);
    }

    if (NetMode != NM_DedicatedServer) {
        if (UGameInstance* GameInstance = InWorld.GetGameInstance()) {
            LocalPlayerAddedDelegateHandle = GameInstance->OnLocalPlayerAddedEvent.AddUObject(
                this, &UGameFeedbackRuntimeSubsystem::OnLocalPlayerAdded);
        }
    }

    if (!bAllReady) {
        StartEnsureTimer(InWorld);
    }
}

/**
 * @brief 注销：停止定时器并退订服务端登录与本地玩家加入事件
 */
void UGameFeedbackRuntimeSubsystem::Deinitialize() {
    if (UWorld* World = GetWorld()) {
        World->GetTimerManager().ClearTimer(EnsureTimer);
        if (LocalPlayerAddedDelegateHandle.IsValid()) {
            if (UGameInstance* GameInstance = World->GetGameInstance()) {
                GameInstance->OnLocalPlayerAddedEvent.Remove(LocalPlayerAddedDelegateHandle);
            }
            LocalPlayerAddedDelegateHandle.Reset();
        }
    }

    if (PostLoginDelegateHandle.IsValid()) {
        FGameModeEvents::GameModePostLoginEvent.Remove(PostLoginDelegateHandle);
        PostLoginDelegateHandle.Reset();
    }

    Super::Deinitialize();
}

/**
 * @brief 确保 GameState、PlayerController 及网络组件按 NetMode 正确装配
 * @param World 当前世界
 * @return 若当前所需组件均已就绪返回 true；若存在待装配对象返回 false
 */
bool UGameFeedbackRuntimeSubsystem::EnsureComponents(UWorld& World) {
    const ENetMode NetMode = World.GetNetMode();
    const bool bIsDedicatedServer = NetMode == NM_DedicatedServer;
    const bool bIsServer = NetMode == NM_ListenServer || NetMode == NM_DedicatedServer;

    bool bGameStateReady = false;
    if (AGameStateBase* GameState = World.GetGameState()) {
        if (!bIsDedicatedServer) {
            GameFeedback::Internal::EnsureComponent<UGameFeedbackWorldComponent>(GameState);
        }
        if (bIsServer) {
            GameFeedback::Internal::EnsureComponent<UGameFeedbackNetworkDispatcherComponent>(GameState);
        }
        bGameStateReady = true;
    }

    int32 FoundControllers = 0;
    for (FConstPlayerControllerIterator Iterator = World.GetPlayerControllerIterator(); Iterator; ++Iterator) {
        APlayerController* PC = Iterator->Get();
        if (!PC) {
            continue;
        }

        ++FoundControllers;
        if (bIsServer && !PC->IsLocalController()) {
            UGameFeedbackReceiverComponent::EnsureOnPlayerController(PC);
        }

        if (!bIsDedicatedServer && PC->IsLocalController()) {
            GameFeedback::Internal::EnsureComponent<UGameFeedbackPlayerComponent>(PC);
        }
    }

    const bool bAllReady = bGameStateReady && (FoundControllers > 0 || bIsDedicatedServer);
    GAME_LOG(Verbose, TEXT("[Runtime] 组件拓扑检查完成: NetMode=%d, GameStateReady=%d, Controllers=%d, Ready=%d"),
        static_cast<int32>(NetMode), bGameStateReady ? 1 : 0, FoundControllers, bAllReady ? 1 : 0);
    return bAllReady;
}

/**
 * @brief 启动延迟补齐定时器，直至全部必备组件装配就绪后自动停止
 * @param World 当前世界
 */
void UGameFeedbackRuntimeSubsystem::StartEnsureTimer(UWorld& World) {
    if (World.GetTimerManager().IsTimerActive(EnsureTimer)) {
        return;
    }

    World.GetTimerManager().SetTimer(
        EnsureTimer,
        FTimerDelegate::CreateWeakLambda(this, [this]() {
            UWorld* CurrentWorld = GetWorld();
            if (!CurrentWorld) {
                return;
            }
            if (EnsureComponents(*CurrentWorld)) {
                CurrentWorld->GetTimerManager().ClearTimer(EnsureTimer);
            }
        }),
        EnsurePollIntervalSeconds,
        true);
}

/**
 * @brief 服务端新玩家登录回调：过滤世界与本地控制器后立即装配网络接收组件
 * @param GameMode 当前游戏模式
 * @param NewPlayer 新登录的玩家控制器
 */
void UGameFeedbackRuntimeSubsystem::OnPostLogin([[maybe_unused]] AGameModeBase* GameMode, APlayerController* NewPlayer) {
    if (!NewPlayer || NewPlayer->GetWorld() != GetWorld() || NewPlayer->IsLocalController()) {
        return;
    }
    UGameFeedbackReceiverComponent::EnsureOnPlayerController(NewPlayer);
}

/**
 * @brief 本地分屏玩家新增回调：触发该玩家的 PlayerComponent 装配或重启延迟补齐
 * @param NewLocalPlayer 新加入的本地玩家
 */
void UGameFeedbackRuntimeSubsystem::OnLocalPlayerAdded(ULocalPlayer* NewLocalPlayer) {
    UWorld* World = GetWorld();
    if (!World || !NewLocalPlayer) {
        return;
    }

    if (APlayerController* PC = NewLocalPlayer->GetPlayerController(World)) {
        if (PC->IsLocalController()) {
            GameFeedback::Internal::EnsureComponent<UGameFeedbackPlayerComponent>(PC);
            return;
        }
    }

    StartEnsureTimer(*World);
}
