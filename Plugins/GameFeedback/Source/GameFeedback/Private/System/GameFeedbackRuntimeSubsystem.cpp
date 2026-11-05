// Copyright Epic Games, Inc. All Rights Reserved.

#include "System/GameFeedbackRuntimeSubsystem.h"

#include "Component/GameFeedbackNetworkDispatcherComponent.h"
#include "Component/GameFeedbackPlayerComponent.h"
#include "Component/GameFeedbackWorldComponent.h"
#include "Network/GameFeedbackReceiverComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

namespace {

/**
 * @brief 确保目标 Actor 上存在指定类型的组件（缺失时创建并注册）
 * @tparam TComponent 组件类型
 * @param Owner 组件宿主
 * @return 已存在或新建的组件指针；宿主为空时返回 nullptr
 */
template <typename TComponent>
TComponent* EnsureComponent(AActor* Owner)
{
    if (!Owner) {
        return nullptr;
    }

    if (TComponent* Existing = Owner->FindComponentByClass<TComponent>()) {
        return Existing;
    }

    TComponent* Component = NewObject<TComponent>(Owner);
    Owner->AddInstanceComponent(Component);
    Component->RegisterComponent();
    return Component;
}

}

/**
 * @brief 仅保留游戏世界
 * @param Outer 外层对象
 * @return 是否创建
 */
bool UGameFeedbackRuntimeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    if (!Super::ShouldCreateSubsystem(Outer)) {
        return false;
    }
    const UWorld* World = Outer ? Outer->GetWorld() : nullptr;
    return World && World->IsGameWorld();
}

/**
 * @brief 世界 BeginPlay：装配现有组件并启动迟到玩家补齐定时器
 * @param InWorld 当前世界
 */
void UGameFeedbackRuntimeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);

    EnsureComponents(InWorld);

    InWorld.GetTimerManager().SetTimer(
        EnsureTimer,
        FTimerDelegate::CreateWeakLambda(this, [this]() {
            UWorld* World = GetWorld();
            if (World) {
                EnsureComponents(*World);
            }
        }),
        0.25f,
        true);
}

/**
 * @brief 注销：停止补齐定时器
 */
void UGameFeedbackRuntimeSubsystem::Deinitialize()
{
    if (UWorld* World = GetWorld()) {
        World->GetTimerManager().ClearTimer(EnsureTimer);
    }
    Super::Deinitialize();
}

/**
 * @brief 确保 GameState、PlayerController 及网络组件按 NetMode 正确装配
 * @param World 当前世界
 */
void UGameFeedbackRuntimeSubsystem::EnsureComponents(UWorld& World)
{
    const ENetMode NetMode = World.GetNetMode();
    const bool bIsDedicatedServer = NetMode == NM_DedicatedServer;
    const bool bIsServer = NetMode == NM_ListenServer || NetMode == NM_DedicatedServer;

    if (AGameStateBase* GameState = World.GetGameState()) {
        if (!bIsDedicatedServer) {
            EnsureComponent<UGameFeedbackWorldComponent>(GameState);
        }
        if (bIsServer) {
            EnsureComponent<UGameFeedbackNetworkDispatcherComponent>(GameState);
        }
    }

    for (FConstPlayerControllerIterator Iterator = World.GetPlayerControllerIterator(); Iterator; ++Iterator) {
        APlayerController* PC = Iterator->Get();
        if (!PC) {
            continue;
        }

        if (bIsServer) {
            UGameFeedbackReceiverComponent::EnsureOnPlayerController(PC);
        }

        if (!bIsDedicatedServer && PC->IsLocalController()) {
            EnsureComponent<UGameFeedbackPlayerComponent>(PC);
        }
    }
}
