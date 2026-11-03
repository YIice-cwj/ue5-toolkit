/**
 * @file GameFeedbackNetworkDispatcherTest.cpp
 * @brief 网络派发器组件频率限制与距离剔除自动化测试
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
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagsManager.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Network/GameFeedbackReceiverComponent.h"

namespace {

/**
 * @brief 创建具有稳定资产路径的测试 Definition
 * @param ObjectPath 完整对象路径（Package.Object）
 * @return 测试 Definition
 */
UGameFeedbackDefinition* CreateDefinitionAtPath(const FString& ObjectPath) {
    FString PackageName;
    FString ObjectName;
    ObjectPath.Split(TEXT("."), &PackageName, &ObjectName);
    return NewObject<UGameFeedbackDefinition>(CreatePackage(*PackageName), *ObjectName);
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackNetworkDispatcherRateAndCullTest,
    "Game.Feedback.NetworkDispatcherRateAndCull",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Dispatcher 单帧派发上限截断与视距距离剔除
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackNetworkDispatcherRateAndCullTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameFeedbackDefinition* Definition = CreateDefinitionAtPath(TEXT("/Game/Feedback/DispatchTest/Def.Def"));
    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Dispatch.Channel")));
    Registry->ExactChannelMap.Add(Channel, Definition);
    Registry->RebuildDefinitionIndex();

    UGameFeedbackSettings* Settings = GetMutableDefault<UGameFeedbackSettings>();
    const TSoftObjectPtr<UGameFeedbackRegistry> PrevRegistry = Settings->GlobalRegistry;
    const int32 PrevMaxDispatch = Settings->MaxDispatchPerFrame;
    const float PrevCullDist = Settings->DefaultNetworkCullDistance;
    Settings->GlobalRegistry = Registry;
    Settings->MaxDispatchPerFrame = 1;
    Settings->DefaultNetworkCullDistance = 2000.0f;

    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameFeedbackDispatcherTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("Dispatcher 测试世界有效"), World);
    if (!World) {
        Settings->GlobalRegistry = PrevRegistry;
        Settings->MaxDispatchPerFrame = PrevMaxDispatch;
        Settings->DefaultNetworkCullDistance = PrevCullDist;
        GameInstance->Shutdown();
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
        Settings->GlobalRegistry = PrevRegistry;
        Settings->MaxDispatchPerFrame = PrevMaxDispatch;
        Settings->DefaultNetworkCullDistance = PrevCullDist;
        GameInstance->Shutdown();
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
    NearPawn->SetActorLocation(FVector(200.0f, 0.0f, 0.0f));
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

    UGameplayMessageSubsystem* Bus = GameInstance->GetSubsystem<UGameplayMessageSubsystem>();
    TestNotNull(TEXT("消息总线有效"), Bus);
    if (!Bus) {
        Settings->GlobalRegistry = PrevRegistry;
        Settings->MaxDispatchPerFrame = PrevMaxDispatch;
        Settings->DefaultNetworkCullDistance = PrevCullDist;
        GameInstance->Shutdown();
        return false;
    }

    FGameFeedbackMessage Msg;
    Msg.Signal.PrimaryChannel = Channel;
    Msg.Signal.HitResult.ImpactPoint = FVector(100.0f, 0.0f, 0.0f);
    Msg.OverrideDefinition = Definition;

    Bus->BroadcastMessage(TAG_Feedback_Channel_Play, Msg);
    TestEqual(TEXT("近处客户端应接收到派发 RPC"), NearDispatches, 1);
    TestEqual(TEXT("超出剔除距离的远端客户端不应接收 RPC"), FarDispatches, 0);

    Bus->BroadcastMessage(TAG_Feedback_Channel_Play, Msg);
    TestEqual(TEXT("单帧达到 MaxDispatchPerFrame 上限后后续报文被截断"), NearDispatches, 1);

    ++GFrameCounter;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Play, Msg);
    TestEqual(TEXT("跨帧后配额重置允许再次派发"), NearDispatches, 2);

    Dispatcher->EndPlay(EEndPlayReason::Destroyed);

    Settings->GlobalRegistry = PrevRegistry;
    Settings->MaxDispatchPerFrame = PrevMaxDispatch;
    Settings->DefaultNetworkCullDistance = PrevCullDist;
    GameInstance->Shutdown();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
