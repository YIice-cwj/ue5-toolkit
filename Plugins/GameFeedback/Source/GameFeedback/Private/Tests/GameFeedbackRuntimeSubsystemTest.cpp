/**
 * @file GameFeedbackRuntimeSubsystemTest.cpp
 * @brief 反馈运行时子系统拓扑装配与世界过滤自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Component/GameFeedbackNetworkDispatcherComponent.h"
#include "Component/GameFeedbackPlayerComponent.h"
#include "Component/GameFeedbackWorldComponent.h"
#include "Engine/DemoNetDriver.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Network/GameFeedbackReceiverComponent.h"
#include "System/GameFeedbackRuntimeSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackRuntimeSubsystemTest,
    "Game.Feedback.RuntimeSubsystem",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 RuntimeSubsystem 组件装配规则、世界类型过滤及定时补齐
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackRuntimeSubsystemTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameFeedbackSubsystemTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("Subsystem 测试世界有效"), World);
    if (!World) {
        GameInstance->Shutdown();
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
        GameInstance->Shutdown();
        return false;
    }
    WorldContext->ActiveNetDrivers.Add(FNamedNetDriver(Driver, nullptr));

    UGameFeedbackRuntimeSubsystem* Subsystem = World->GetSubsystem<UGameFeedbackRuntimeSubsystem>();
    TestNotNull(TEXT("RuntimeSubsystem 实例有效"), Subsystem);
    TestTrue(TEXT("Game 世界必须允许创建 RuntimeSubsystem"), Subsystem->ShouldCreateSubsystem(World));

    AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
    World->SetGameState(GameState);
    APlayerController* ServerPC = World->SpawnActor<APlayerController>();
    World->AddController(ServerPC);

    Subsystem->EnsureComponents(*World);

    TestNotNull(
        TEXT("DS 服务端必须为 GameState 装配 NetworkDispatcherComponent"),
        GameState->FindComponentByClass<UGameFeedbackNetworkDispatcherComponent>());
    TestNull(
        TEXT("DS 服务端严禁为 GameState 装配 WorldComponent"),
        GameState->FindComponentByClass<UGameFeedbackWorldComponent>());
    TestNotNull(
        TEXT("DS 服务端必须为 PlayerController 装配 ReceiverComponent"),
        ServerPC->FindComponentByClass<UGameFeedbackReceiverComponent>());
    TestNull(
        TEXT("DS 服务端严禁为 PlayerController 装配 PlayerComponent"),
        ServerPC->FindComponentByClass<UGameFeedbackPlayerComponent>());

    World->SetPlayInEditorInitialNetMode(NM_ListenServer);
    Subsystem->EnsureComponents(*World);

    TestNotNull(
        TEXT("ListenServer 必须为 GameState 装配 WorldComponent"),
        GameState->FindComponentByClass<UGameFeedbackWorldComponent>());

    Subsystem->Deinitialize();
    GameInstance->Shutdown();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
