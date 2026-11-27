/**
 * @file GameFeedbackWorldComponentTest.cpp
 * @brief 世界反馈组件本地广播、多闪白目标模式、分屏交错网络去重与清除自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Asset/GameFeedbackDefinition.h"
#include "Asset/GameFeedbackRegistry.h"
#include "Bus/GameFeedbackChannels.h"
#include "Bus/GameFeedbackMessages.h"
#include "Component/GameFeedbackWorldComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Config/GameFeedbackSettings.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayTagsManager.h"
#include "Materials/Material.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Tests/GameFeedbackTestHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackWorldComponentTest,
    "Game.Feedback.WorldComponent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 WorldComponent 三种闪白模式、分屏交错同帧网络去重与 Clear 还原
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackWorldComponentTest::RunTest([[maybe_unused]] const FString& Parameters) {
    GameFeedbackTestHelpers::FScopedFeedbackSettingsGuard SettingsGuard;
    UGameFeedbackSettings* Settings = SettingsGuard.Get();

    UGameFeedbackDefinition* Definition = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/WorldTest/Def.Def"));
    Definition->HitFlash.bEnable = true;
    Definition->HitFlash.TargetMode = EGameFeedbackFlashTargetMode::HitComponentOnly;
    Definition->HitFlash.Duration = 0.2f;
    Definition->HitFlash.OverlayMaterial = NewObject<UMaterial>(GetTransientPackage());

    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.WorldComp.Channel")));
    Registry->ExactChannelMap.Add(Channel, Definition);
    Registry->RebuildDefinitionIndex();
    Settings->GlobalRegistry = Registry;

    GameFeedbackTestHelpers::FScopedStandaloneFeedbackWorld TestEnv(FName(TEXT("GameFeedbackWorldComponentTestWorld")));
    UWorld* World = TestEnv.GetWorld();
    TestNotNull(TEXT("WorldComponent 测试世界有效"), World);
    if (!World) {
        return false;
    }

    UGameplayMessageSubsystem* Bus = TestEnv.GetGameInstance()->GetSubsystem<UGameplayMessageSubsystem>();
    TestNotNull(TEXT("消息总线有效"), Bus);
    if (!Bus) {
        return false;
    }

    AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
    UGameFeedbackWorldComponent* WorldComp = NewObject<UGameFeedbackWorldComponent>(GameState);
    GameState->AddInstanceComponent(WorldComp);
    WorldComp->RegisterComponent();
    WorldComp->BeginPlay();

    AActor* Attacker = World->SpawnActor<AActor>();
    AActor* TargetA = World->SpawnActor<AActor>();
    UStaticMeshComponent* TargetMeshA = NewObject<UStaticMeshComponent>(TargetA);
    TargetA->SetRootComponent(TargetMeshA);
    TargetMeshA->RegisterComponent();

    USkeletalMeshComponent* SkelMesh = NewObject<USkeletalMeshComponent>(TargetA);
    SkelMesh->SetupAttachment(TargetMeshA);
    SkelMesh->RegisterComponent();

    UStaticMeshComponent* TaggedMesh = NewObject<UStaticMeshComponent>(TargetA);
    TaggedMesh->ComponentTags.Add(FName(TEXT("Flashable")));
    TaggedMesh->SetupAttachment(TargetMeshA);
    TaggedMesh->RegisterComponent();

    AActor* TargetB = World->SpawnActor<AActor>();
    UStaticMeshComponent* TargetMeshB = NewObject<UStaticMeshComponent>(TargetB);
    TargetB->SetRootComponent(TargetMeshB);
    TargetMeshB->RegisterComponent();

    FGameFeedbackSignal SignalA;
    SignalA.PrimaryChannel = Channel;
    SignalA.Instigator = Attacker;
    SignalA.Target = TargetA;
    SignalA.HitResult.ImpactPoint = FVector(100.0f, 0.0f, 0.0f);
    SignalA.HitResult.Component = TargetMeshA;
    SignalA.Magnitude = 10.0f;

    FGameFeedbackMessage PlayMsg;
    PlayMsg.Signal = SignalA;
    PlayMsg.OverrideDefinition = Definition;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Play, PlayMsg);
    TestTrue(TEXT("HitComponentOnly 模式应对命中网格应用闪白材质"), TargetMeshA->GetOverlayMaterial() == Definition->HitFlash.OverlayMaterial.Get());
    TestNull(TEXT("HitComponentOnly 模式不得影响未命中的其他骨骼网格"), SkelMesh->GetOverlayMaterial());

    FGameFeedbackClearMessage ClearMsg;
    ClearMsg.Target = TargetA;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Clear, ClearMsg);
    TestNull(TEXT("Clear 消息必须清除目标闪白材质"), TargetMeshA->GetOverlayMaterial());

    Definition->HitFlash.TargetMode = EGameFeedbackFlashTargetMode::AllSkeletalMeshes;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Play, PlayMsg);
    TestTrue(TEXT("AllSkeletalMeshes 模式应对骨骼网格应用闪白"), SkelMesh->GetOverlayMaterial() == Definition->HitFlash.OverlayMaterial.Get());
    TestNull(TEXT("AllSkeletalMeshes 模式不得影响静态网格"), TargetMeshA->GetOverlayMaterial());
    Bus->BroadcastMessage(TAG_Feedback_Channel_Clear, ClearMsg);

    Definition->HitFlash.TargetMode = EGameFeedbackFlashTargetMode::TaggedComponentsOnly;
    Definition->HitFlash.ComponentFilterTag = FName(TEXT("Flashable"));
    Bus->BroadcastMessage(TAG_Feedback_Channel_Play, PlayMsg);
    TestTrue(TEXT("TaggedComponentsOnly 模式应对携带指定 Tag 的网格应用闪白"), TaggedMesh->GetOverlayMaterial() == Definition->HitFlash.OverlayMaterial.Get());
    TestNull(TEXT("TaggedComponentsOnly 模式不得影响未带 Tag 的网格"), TargetMeshA->GetOverlayMaterial());
    Bus->BroadcastMessage(TAG_Feedback_Channel_Clear, ClearMsg);

    Definition->HitFlash.TargetMode = EGameFeedbackFlashTargetMode::HitComponentOnly;
    ++GFrameCounter;
    FGameFeedbackNetworkMessage NetMsgA;
    NetMsgA.Signal = SignalA;
    NetMsgA.ResolvedDefinition = Definition;
    NetMsgA.bSuppressed = true;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Network, NetMsgA);
    TestNull(TEXT("抑制的网络消息不得应用表现"), TargetMeshA->GetOverlayMaterial());

    FGameFeedbackSignal SignalB = SignalA;
    SignalB.Target = TargetB;
    SignalB.HitResult.Component = TargetMeshB;
    SignalB.HitResult.ImpactPoint = FVector(200.0f, 0.0f, 0.0f);
    FGameFeedbackNetworkMessage NetMsgB;
    NetMsgB.Signal = SignalB;
    NetMsgB.ResolvedDefinition = Definition;
    NetMsgB.bSuppressed = false;

    NetMsgA.bSuppressed = false;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Network, NetMsgA);
    Bus->BroadcastMessage(TAG_Feedback_Channel_Network, NetMsgB);
    TestTrue(TEXT("合法网络消息 A 必须应用表现"), TargetMeshA->GetOverlayMaterial() == Definition->HitFlash.OverlayMaterial.Get());
    TestTrue(TEXT("合法网络消息 B 必须应用表现"), TargetMeshB->GetOverlayMaterial() == Definition->HitFlash.OverlayMaterial.Get());

    TargetMeshA->SetOverlayMaterial(nullptr);
    TargetMeshB->SetOverlayMaterial(nullptr);
    Bus->BroadcastMessage(TAG_Feedback_Channel_Network, NetMsgA);
    Bus->BroadcastMessage(TAG_Feedback_Channel_Network, NetMsgB);
    TestNull(TEXT("分屏交错到达（A-B-A-B）时同帧重复消息 A 必须被去重抑制"), TargetMeshA->GetOverlayMaterial());
    TestNull(TEXT("分屏交错到达（A-B-A-B）时同帧重复消息 B 必须被去重抑制"), TargetMeshB->GetOverlayMaterial());

    ++GFrameCounter;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Network, NetMsgA);
    TestTrue(TEXT("新帧下相同网络消息重新放行"), TargetMeshA->GetOverlayMaterial() == Definition->HitFlash.OverlayMaterial.Get());

    WorldComp->EndPlay(EEndPlayReason::Destroyed);
    TestNull(TEXT("EndPlay 后闪白被完全还原并注销"), TargetMeshA->GetOverlayMaterial());
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
