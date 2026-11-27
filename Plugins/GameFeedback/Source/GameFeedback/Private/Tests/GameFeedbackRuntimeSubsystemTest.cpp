/**
 * @file GameFeedbackRuntimeSubsystemTest.cpp
 * @brief 反馈运行时子系统拓扑装配、ListenServer 本地免装 Receiver 与跨世界 PostLogin 过滤自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Component/GameFeedbackNetworkDispatcherComponent.h"
#include "Component/GameFeedbackPlayerComponent.h"
#include "Component/GameFeedbackWorldComponent.h"
#include "Engine/DemoNetDriver.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"
#include "Network/GameFeedbackReceiverComponent.h"
#include "System/GameFeedbackRuntimeSubsystem.h"
#include "Tests/GameFeedbackTestHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackRuntimeSubsystemTest,
    "Game.Feedback.RuntimeSubsystem",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 RuntimeSubsystem 跨 NetMode 组件装配规则、本地 PC 免装 Receiver 及多世界 PostLogin 隔离
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackRuntimeSubsystemTest::RunTest([[maybe_unused]] const FString& Parameters) {
    GameFeedbackTestHelpers::FScopedStandaloneFeedbackWorld TestEnv(FName(TEXT("GameFeedbackSubsystemTestWorld")));
    UWorld* World = TestEnv.GetWorld();
    TestNotNull(TEXT("Subsystem 测试世界有效"), World);
    if (!World) {
        return false;
    }

    World->WorldType = EWorldType::PIE;
    World->SetPlayInEditorInitialNetMode(NM_DedicatedServer);

    UDemoNetDriver* Driver = NewObject<UDemoNetDriver>(GetTransientPackage());
    Driver->NetDriverName = NAME_GameNetDriver;
    Driver->World = World;
    FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(World);
    TestNotNull(TEXT("WorldContext 有效"), WorldContext);
    if (!WorldContext) {
        GameFeedbackTestHelpers::CleanupTestDemoNetDriver(Driver);
        return false;
    }
    WorldContext->ActiveNetDrivers.Add(FNamedNetDriver(Driver, nullptr));

    UGameFeedbackRuntimeSubsystem* Subsystem = World->GetSubsystem<UGameFeedbackRuntimeSubsystem>();
    TestNotNull(TEXT("RuntimeSubsystem 实例有效"), Subsystem);
    if (!Subsystem) {
        GameFeedbackTestHelpers::CleanupTestDemoNetDriver(Driver, WorldContext);
        return false;
    }
    TestTrue(TEXT("Game 世界必须允许创建 RuntimeSubsystem"), Subsystem->ShouldCreateSubsystem(World));

    AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
    World->SetGameState(GameState);
    APlayerController* RemotePC = World->SpawnActor<APlayerController>();
    World->AddController(RemotePC);

    Subsystem->OnWorldBeginPlay(*World);

    ON_SCOPE_EXIT {
        Subsystem->Deinitialize();
        GameFeedbackTestHelpers::CleanupTestDemoNetDriver(Driver, WorldContext);
    };

    TestNotNull(
        TEXT("DS 服务端必须为 GameState 装配 NetworkDispatcherComponent"),
        GameState->FindComponentByClass<UGameFeedbackNetworkDispatcherComponent>());
    TestNull(
        TEXT("DS 服务端严禁为 GameState 装配 WorldComponent"),
        GameState->FindComponentByClass<UGameFeedbackWorldComponent>());
    TestNotNull(
        TEXT("DS 服务端必须为远端 PlayerController 装配 ReceiverComponent"),
        RemotePC->FindComponentByClass<UGameFeedbackReceiverComponent>());
    TestNull(
        TEXT("DS 服务端严禁为 PlayerController 装配 PlayerComponent"),
        RemotePC->FindComponentByClass<UGameFeedbackPlayerComponent>());

    World->SetPlayInEditorInitialNetMode(NM_ListenServer);
    APlayerController* HostLocalPC = World->SpawnActor<APlayerController>();
    ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
    HostLocalPC->SetPlayer(LocalPlayer);
    World->AddController(HostLocalPC);

    Subsystem->EnsureComponents(*World);
    TestNotNull(
        TEXT("ListenServer 必须为 GameState 装配 WorldComponent"),
        GameState->FindComponentByClass<UGameFeedbackWorldComponent>());
    TestNotNull(
        TEXT("ListenServer 必须为本地主机 PlayerController 装配 PlayerComponent"),
        HostLocalPC->FindComponentByClass<UGameFeedbackPlayerComponent>());
    TestNull(
        TEXT("ListenServer 本地主机 PC 不应装配冗余复制 ReceiverComponent"),
        HostLocalPC->FindComponentByClass<UGameFeedbackReceiverComponent>());

    GameFeedbackTestHelpers::FScopedLightweightGameWorld ForeignWorldGuard;
    UWorld* ForeignWorld = ForeignWorldGuard.GetWorld();
    APlayerController* ForeignPC = ForeignWorld ? ForeignWorld->SpawnActor<APlayerController>() : nullptr;
    TestNotNull(TEXT("异世界 PC 创建成功"), ForeignPC);
    if (ForeignPC) {
        FGameModeEvents::GameModePostLoginEvent.Broadcast(nullptr, ForeignPC);
        TestNull(
            TEXT("跨世界 PostLogin 广播严禁污染异世界 PlayerController"),
            ForeignPC->FindComponentByClass<UGameFeedbackReceiverComponent>());
    }

    APlayerController* LateJoinRemotePC = World->SpawnActor<APlayerController>();
    FGameModeEvents::GameModePostLoginEvent.Broadcast(nullptr, LateJoinRemotePC);
    TestNotNull(
        TEXT("同世界新登录远端 PlayerController 必须经 PostLogin 立即装配 ReceiverComponent"),
        LateJoinRemotePC->FindComponentByClass<UGameFeedbackReceiverComponent>());

    GameFeedbackTestHelpers::FScopedStandaloneFeedbackWorld SplitTestEnv(FName(TEXT("GameFeedbackSplitHotJoinWorld")));
    UWorld* SplitWorld = SplitTestEnv.GetWorld();
    TestNotNull(TEXT("分屏热加入测试世界有效"), SplitWorld);
    if (SplitWorld) {
        SplitWorld->WorldType = EWorldType::PIE;
        SplitWorld->SetPlayInEditorInitialNetMode(NM_ListenServer);
        UGameFeedbackRuntimeSubsystem* SplitSubsystem = SplitWorld->GetSubsystem<UGameFeedbackRuntimeSubsystem>();
        TestNotNull(TEXT("分屏世界 RuntimeSubsystem 有效"), SplitSubsystem);
        if (SplitSubsystem) {
            AGameStateBase* SplitGameState = SplitWorld->SpawnActor<AGameStateBase>();
            SplitWorld->SetGameState(SplitGameState);
            SplitSubsystem->OnWorldBeginPlay(*SplitWorld);

            APlayerController* SplitHotJoinPC = SplitWorld->SpawnActor<APlayerController>();
            ULocalPlayer* SplitHotJoinLocalPlayer = NewObject<ULocalPlayer>(GEngine);
            SplitHotJoinPC->SetPlayer(SplitHotJoinLocalPlayer);
            SplitHotJoinLocalPlayer->PlayerController = SplitHotJoinPC;
            SplitWorld->AddController(SplitHotJoinPC);

            TestNull(
                TEXT("热加入事件广播前分屏玩家尚未挂载 PlayerComponent"),
                SplitHotJoinPC->FindComponentByClass<UGameFeedbackPlayerComponent>());
            SplitTestEnv.GetGameInstance()->OnLocalPlayerAddedEvent.Broadcast(SplitHotJoinLocalPlayer);
            TestNotNull(
                TEXT("本地分屏热加入玩家必须经 OnLocalPlayerAddedEvent 立即装配 PlayerComponent"),
                SplitHotJoinPC->FindComponentByClass<UGameFeedbackPlayerComponent>());

            SplitSubsystem->Deinitialize();
            SplitHotJoinLocalPlayer->PlayerController = nullptr;
            SplitHotJoinPC->Player = nullptr;
        }
    }

    HostLocalPC->Player = nullptr;
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
