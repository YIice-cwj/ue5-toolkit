/**
 * @file GameFeedbackPlayerComponentTest.cpp
 * @brief 玩家反馈组件本地预测、网络抑制与镜头震动追踪自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Asset/GameFeedbackDefinition.h"
#include "Asset/GameFeedbackRegistry.h"
#include "Bus/GameFeedbackChannels.h"
#include "Bus/GameFeedbackMessages.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Component/GameFeedbackPlayerComponent.h"
#include "Config/GameFeedbackSettings.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagsManager.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Tests/GameFeedbackTestHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackPlayerComponentTest,
    "Game.Feedback.PlayerComponent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 PlayerComponent 本地预测添加与消费、网络抑制客观跳过震屏及生命周期退订
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackPlayerComponentTest::RunTest([[maybe_unused]] const FString& Parameters) {
    GameFeedbackTestHelpers::FScopedFeedbackSettingsGuard SettingsGuard;
    UGameFeedbackSettings* Settings = SettingsGuard.Get();

    UGameFeedbackDefinition* Definition = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/PlayerTest/Def.Def"));
    Definition->CameraShake.MinMagnitude = 1.0f;
    Definition->CameraShake.AttackerShakeClass = UCameraShakeBase::StaticClass();
    Definition->CameraShake.AttackerShakeScale = 1.0f;
    Definition->CameraShake.VictimShakeClass = UCameraShakeBase::StaticClass();
    Definition->CameraShake.VictimShakeScale = 1.5f;

    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.PlayerComp.Channel")));
    Registry->ExactChannelMap.Add(Channel, Definition);
    Registry->RebuildDefinitionIndex();
    Settings->GlobalRegistry = Registry;

    GameFeedbackTestHelpers::FScopedStandaloneFeedbackWorld TestEnv(FName(TEXT("GameFeedbackPlayerComponentTestWorld")));
    UWorld* World = TestEnv.GetWorld();
    TestNotNull(TEXT("PlayerComponent 测试世界有效"), World);
    if (!World) {
        return false;
    }

    UGameplayMessageSubsystem* Bus = TestEnv.GetGameInstance()->GetSubsystem<UGameplayMessageSubsystem>();
    TestNotNull(TEXT("消息总线有效"), Bus);
    if (!Bus) {
        return false;
    }

    APlayerController* LocalPC = World->SpawnActor<APlayerController>();
    ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
    LocalPC->SetPlayer(LocalPlayer);
    LocalPC->PlayerCameraManager = World->SpawnActor<APlayerCameraManager>();

    UGameFeedbackPlayerComponent* PlayerComp = NewObject<UGameFeedbackPlayerComponent>(LocalPC);
    LocalPC->AddInstanceComponent(PlayerComp);
    PlayerComp->RegisterComponent();

    FGameFeedbackSignal TestSignal;
    TestSignal.PrimaryChannel = Channel;
    TestSignal.Instigator = LocalPC;
    TestSignal.Magnitude = 10.0f;
    TestSignal.PredictionEventId = 77;

    TestFalse(TEXT("未播放预测前消费应返回 false"), PlayerComp->ConsumePrediction(TestSignal));

    World->WorldType = EWorldType::PIE;
    World->SetPlayInEditorInitialNetMode(NM_Client);

    PlayerComp->BeginPlay();

    FGameFeedbackMessage PlayMsg;
    PlayMsg.Signal = TestSignal;
    PlayMsg.OverrideDefinition = Definition;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Play, PlayMsg);

    TestTrue(TEXT("本地 Client 模式下 Play 广播应将事件加入预测记录"), PlayerComp->ConsumePrediction(TestSignal));
    TestFalse(TEXT("预测指纹一次消费后不得二次消费"), PlayerComp->ConsumePrediction(TestSignal));
    TestEqual(TEXT("Play 广播应触发一次镜头震动追踪"), PlayerComp->GetTrackedShakeCount(), 1);

    PlayerComp->EndPlay(EEndPlayReason::Destroyed);
    TestEqual(TEXT("EndPlay 后应停止并清空镜头震动追踪"), PlayerComp->GetTrackedShakeCount(), 0);

    PlayerComp->BeginPlay();
    FGameFeedbackNetworkMessage NetMsg;
    NetMsg.Signal = TestSignal;
    NetMsg.ResolvedDefinition = Definition;
    NetMsg.bSuppressed = true;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Network, NetMsg);
    TestEqual(TEXT("受抑制的网络消息不得触发镜头震动"), PlayerComp->GetTrackedShakeCount(), 0);

    NetMsg.bSuppressed = false;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Network, NetMsg);
    TestEqual(TEXT("未抑制的网络消息应触发镜头震动追踪"), PlayerComp->GetTrackedShakeCount(), 1);

    PlayerComp->EndPlay(EEndPlayReason::Destroyed);
    PlayerComp->BeginPlay();

    APlayerController* SecondLocalPC = World->SpawnActor<APlayerController>();
    ULocalPlayer* SecondLocalPlayer = NewObject<ULocalPlayer>(GEngine);
    SecondLocalPC->SetPlayer(SecondLocalPlayer);
    SecondLocalPC->PlayerCameraManager = World->SpawnActor<APlayerCameraManager>();
    UGameFeedbackPlayerComponent* SecondPlayerComp = NewObject<UGameFeedbackPlayerComponent>(SecondLocalPC);
    SecondLocalPC->AddInstanceComponent(SecondPlayerComp);
    SecondPlayerComp->RegisterComponent();
    SecondPlayerComp->BeginPlay();

    FGameFeedbackNetworkMessage TargetedNetMsg = NetMsg;
    TargetedNetMsg.Signal.Instigator = LocalPC;
    TargetedNetMsg.Signal.Target = SecondLocalPC;
    TargetedNetMsg.ReceivingController = LocalPC;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Network, TargetedNetMsg);
    TestEqual(TEXT("分屏定向至 LocalPC 的网络回执应仅触发 LocalPC 玩家组件"), PlayerComp->GetTrackedShakeCount(), 1);
    TestEqual(TEXT("分屏定向至 LocalPC 的网络回执不得扇出触发 SecondLocalPC"), SecondPlayerComp->GetTrackedShakeCount(), 0);

    TargetedNetMsg.ReceivingController = SecondLocalPC;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Network, TargetedNetMsg);
    TestEqual(TEXT("分屏定向至 SecondLocalPC 的网络回执应触发 SecondLocalPC 玩家组件"), SecondPlayerComp->GetTrackedShakeCount(), 1);

    SecondPlayerComp->EndPlay(EEndPlayReason::Destroyed);
    SecondLocalPC->Player = nullptr;
    LocalPC->Player = nullptr;
    PlayerComp->EndPlay(EEndPlayReason::Destroyed);
    TestEqual(TEXT("再次 EndPlay 后状态归零"), PlayerComp->GetTrackedShakeCount(), 0);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
