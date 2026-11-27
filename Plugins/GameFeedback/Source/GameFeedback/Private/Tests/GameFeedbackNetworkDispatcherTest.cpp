/**
 * @file GameFeedbackNetworkDispatcherTest.cpp
 * @brief 网络派发器组件频率限制、零受众免耗配额与距离剔除自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Asset/GameFeedbackDefinition.h"
#include "Asset/GameFeedbackRegistry.h"
#include "Bus/GameFeedbackChannels.h"
#include "Bus/GameFeedbackMessages.h"
#include "Component/GameFeedbackNetworkDispatcherComponent.h"
#include "Config/GameFeedbackSettings.h"
#include "Engine/DemoNetDriver.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagsManager.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Misc/ScopeExit.h"
#include "Network/GameFeedbackReceiverComponent.h"
#include "Tests/GameFeedbackTestHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackNetworkDispatcherRateAndCullTest,
    "Game.Feedback.NetworkDispatcherRateAndCull",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Dispatcher 单帧派发上限截断、零受众免消耗配额与视距距离剔除
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackNetworkDispatcherRateAndCullTest::RunTest([[maybe_unused]] const FString& Parameters) {
    GameFeedbackTestHelpers::FScopedFeedbackSettingsGuard SettingsGuard;
    UGameFeedbackSettings* Settings = SettingsGuard.Get();

    UGameFeedbackDefinition* Definition = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/DispatchTest/Def.Def"));
    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Dispatch.Channel")));
    Registry->ExactChannelMap.Add(Channel, Definition);
    Registry->RebuildDefinitionIndex();

    Settings->GlobalRegistry = Registry;
    Settings->MaxDispatchPerFrame = 1;
    Settings->DefaultNetworkCullDistance = 2000.0f;

    GameFeedbackTestHelpers::FScopedStandaloneFeedbackWorld TestEnv(FName(TEXT("GameFeedbackDispatcherTestWorld")));
    UWorld* World = TestEnv.GetWorld();
    TestNotNull(TEXT("Dispatcher 测试世界有效"), World);
    if (!World) {
        return false;
    }

    World->WorldType = EWorldType::PIE;
    World->SetPlayInEditorInitialNetMode(NM_ListenServer);

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

    USimulatedClientNetConnection* ConnectionNear = NewObject<USimulatedClientNetConnection>(Driver);
    ConnectionNear->Driver = Driver;
    ConnectionNear->SetInternalAck(true);

    USimulatedClientNetConnection* ConnectionFar = NewObject<USimulatedClientNetConnection>(Driver);
    ConnectionFar->Driver = Driver;
    ConnectionFar->SetInternalAck(true);

    APlayerController* NearPC = World->SpawnActor<APlayerController>();
    NearPC->SetReplicates(true);
    NearPC->SetPlayer(ConnectionNear);
    APawn* NearPawn = World->SpawnActor<APawn>();
    USceneComponent* NearRoot = NewObject<USceneComponent>(NearPawn);
    NearPawn->SetRootComponent(NearRoot);
    NearRoot->RegisterComponent();
    NearPawn->SetActorLocation(FVector::ZeroVector);
    NearPC->Possess(NearPawn);
    NearPC->SetViewTarget(NearPawn);
    World->AddController(NearPC);

    APlayerController* FarPC = World->SpawnActor<APlayerController>();
    FarPC->SetReplicates(true);
    FarPC->SetPlayer(ConnectionFar);
    APawn* FarPawn = World->SpawnActor<APawn>();
    USceneComponent* FarRoot = NewObject<USceneComponent>(FarPawn);
    FarPawn->SetRootComponent(FarRoot);
    FarRoot->RegisterComponent();
    FarPawn->SetActorLocation(FVector(50000.0f, 0.0f, 0.0f));
    FarPC->Possess(FarPawn);
    FarPC->SetViewTarget(FarPawn);
    World->AddController(FarPC);

    UGameFeedbackReceiverComponent* NearReceiver = UGameFeedbackReceiverComponent::EnsureOnPlayerController(NearPC);
    UGameFeedbackReceiverComponent* FarReceiver = UGameFeedbackReceiverComponent::EnsureOnPlayerController(FarPC);
    TestNotNull(TEXT("近端 Receiver 有效"), NearReceiver);
    TestNotNull(TEXT("远端 Receiver 有效"), FarReceiver);

    int32 NearDispatches = 0;
    int32 FarDispatches = 0;
    Driver->SendRPCDel.BindLambda(
        [&NearDispatches, &FarDispatches, NearPC, FarPC](
            AActor* Actor,
            UFunction* Function,
            void*,
            FOutParmRec*,
            FFrame*,
            UObject*,
            bool& bOutBlockSendRPC) {
            if (Function->GetFName() == GET_FUNCTION_NAME_CHECKED(UGameFeedbackReceiverComponent, ClientReceiveFeedback)) {
                if (Actor == NearPC) {
                    ++NearDispatches;
                } else if (Actor == FarPC) {
                    ++FarDispatches;
                }
                bOutBlockSendRPC = true;
            }
        });

    AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
    World->SetGameState(GameState);
    UGameFeedbackNetworkDispatcherComponent* Dispatcher = NewObject<UGameFeedbackNetworkDispatcherComponent>(GameState);
    GameState->AddInstanceComponent(Dispatcher);
    Dispatcher->RegisterComponent();
    Dispatcher->BeginPlay();

    ON_SCOPE_EXIT {
        Dispatcher->EndPlay(EEndPlayReason::Destroyed);
        NearPC->Player = nullptr;
        FarPC->Player = nullptr;
        GameFeedbackTestHelpers::CleanupSimulatedConnection(ConnectionNear);
        GameFeedbackTestHelpers::CleanupSimulatedConnection(ConnectionFar);
        GameFeedbackTestHelpers::CleanupTestDemoNetDriver(Driver, WorldContext);
    };

    UGameplayMessageSubsystem* Bus = TestEnv.GetGameInstance()->GetSubsystem<UGameplayMessageSubsystem>();
    TestNotNull(TEXT("消息总线有效"), Bus);
    if (!Bus) {
        return false;
    }

    FGameFeedbackMessage InvisibleMsg;
    InvisibleMsg.Signal.PrimaryChannel = Channel;
    InvisibleMsg.Signal.HitResult.ImpactPoint = FVector(900000.0f, 0.0f, 0.0f);
    InvisibleMsg.OverrideDefinition = Definition;

    Bus->BroadcastMessage(TAG_Feedback_Channel_Play, InvisibleMsg);
    TestEqual(TEXT("位于原点的近端 Pawn 超出裁剪距离时必须正确裁剪而非因原点跳过裁剪"), NearDispatches, 0);
    TestEqual(TEXT("远端 Pawn 超出裁剪距离时必须被裁剪"), FarDispatches, 0);

    FGameFeedbackMessage VisibleMsg;
    VisibleMsg.Signal.PrimaryChannel = Channel;
    VisibleMsg.Signal.HitResult.ImpactPoint = FVector(100.0f, 0.0f, 0.0f);
    VisibleMsg.OverrideDefinition = Definition;

    Bus->BroadcastMessage(TAG_Feedback_Channel_Play, VisibleMsg);
    TestEqual(TEXT("零受众事件不消耗单帧配额，随后可见近端客户端应成功收到 RPC"), NearDispatches, 1);
    TestEqual(TEXT("超出剔除距离的远端客户端不应接收 RPC"), FarDispatches, 0);

    Bus->BroadcastMessage(TAG_Feedback_Channel_Play, VisibleMsg);
    TestEqual(TEXT("单帧达到 MaxDispatchPerFrame 上限后后续报文被截断"), NearDispatches, 1);

    ++GFrameCounter;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Play, VisibleMsg);
    TestEqual(TEXT("跨帧后配额重置允许再次派发"), NearDispatches, 2);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
