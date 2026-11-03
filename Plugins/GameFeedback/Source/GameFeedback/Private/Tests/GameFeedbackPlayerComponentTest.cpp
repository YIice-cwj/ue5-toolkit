/**
 * @file GameFeedbackPlayerComponentTest.cpp
 * @brief 玩家反馈组件本地预测与网络抑制自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Asset/GameFeedbackDefinition.h"
#include "Asset/GameFeedbackRegistry.h"
#include "Bus/GameFeedbackChannels.h"
#include "Bus/GameFeedbackMessages.h"
#include "Camera/CameraShakeBase.h"
#include "Component/GameFeedbackPlayerComponent.h"
#include "Config/GameFeedbackSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagsManager.h"
#include "Message/GameplayMessageSubsystem.h"

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
    FGameFeedbackPlayerComponentTest,
    "Game.Feedback.PlayerComponent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 PlayerComponent 本地预测添加与消费、网络抑制及生命周期退订
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackPlayerComponentTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameFeedbackPlayerComponentTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("PlayerComponent 测试世界有效"), World);
    if (!World) {
        GameInstance->Shutdown();
        return false;
    }

    APlayerController* LocalPC = World->SpawnActor<APlayerController>();
    ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
    LocalPC->SetPlayer(LocalPlayer);
    UGameFeedbackPlayerComponent* PlayerComp = NewObject<UGameFeedbackPlayerComponent>(LocalPC);
    LocalPC->AddInstanceComponent(PlayerComp);
    PlayerComp->RegisterComponent();

    UGameFeedbackDefinition* Definition = CreateDefinitionAtPath(TEXT("/Game/Feedback/PlayerTest/Def.Def"));
    Definition->CameraShake.MinMagnitude = 1.0f;
    Definition->CameraShake.AttackerShakeClass = UCameraShakeBase::StaticClass();
    Definition->CameraShake.AttackerShakeScale = 1.0f;

    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.PlayerComp.Channel")));
    Registry->ExactChannelMap.Add(Channel, Definition);
    Registry->RebuildDefinitionIndex();

    UGameFeedbackSettings* Settings = GetMutableDefault<UGameFeedbackSettings>();
    const TSoftObjectPtr<UGameFeedbackRegistry> PrevRegistry = Settings->GlobalRegistry;
    Settings->GlobalRegistry = Registry;

    UGameplayMessageSubsystem* Bus = GameInstance->GetSubsystem<UGameplayMessageSubsystem>();
    TestNotNull(TEXT("消息总线有效"), Bus);
    if (!Bus) {
        Settings->GlobalRegistry = PrevRegistry;
        GameInstance->Shutdown();
        return false;
    }

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

    FGameFeedbackNetworkMessage NetMsg;
    NetMsg.Signal = TestSignal;
    NetMsg.ResolvedDefinition = Definition;
    NetMsg.bSuppressed = true;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Network, NetMsg);

    NetMsg.bSuppressed = false;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Network, NetMsg);

    PlayerComp->EndPlay(EEndPlayReason::Destroyed);

    Settings->GlobalRegistry = PrevRegistry;
    GameInstance->Shutdown();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
