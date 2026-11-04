/**
 * @file GameFeedbackWorldComponentTest.cpp
 * @brief 世界反馈组件本地广播、网络去重与清除自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Asset/GameFeedbackDefinition.h"
#include "Asset/GameFeedbackRegistry.h"
#include "Bus/GameFeedbackChannels.h"
#include "Bus/GameFeedbackMessages.h"
#include "Component/GameFeedbackWorldComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Config/GameFeedbackSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayTagsManager.h"
#include "Materials/Material.h"
#include "Message/GameplayMessageSubsystem.h"

namespace {

/**
 * @brief 创建具有稳定资产路径的测试 Definition
 * @param ObjectPath 完整对象路径（Package.Object）
 * @return 测试 Definition
 */
UGameFeedbackDefinition* CreateWorldDefinitionAtPath(const FString& ObjectPath) {
    FString PackageName;
    FString ObjectName;
    ObjectPath.Split(TEXT("."), &PackageName, &ObjectName);
    return NewObject<UGameFeedbackDefinition>(CreatePackage(*PackageName), *ObjectName);
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackWorldComponentTest,
    "Game.Feedback.WorldComponent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 WorldComponent 本地广播响应、网络同帧去重与 Clear 消息传递
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackWorldComponentTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameFeedbackWorldComponentTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("WorldComponent 测试世界有效"), World);
    if (!World) {
        GameInstance->Shutdown();
        return false;
    }

    AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
    UGameFeedbackWorldComponent* WorldComp = NewObject<UGameFeedbackWorldComponent>(GameState);
    GameState->AddInstanceComponent(WorldComp);
    WorldComp->RegisterComponent();

    UGameFeedbackDefinition* Definition = CreateWorldDefinitionAtPath(TEXT("/Game/Feedback/WorldTest/Def.Def"));
    Definition->HitFlash.bEnable = true;
    Definition->HitFlash.TargetMode = EGameFeedbackFlashTargetMode::HitComponentOnly;
    Definition->HitFlash.Duration = 0.2f;
    Definition->HitFlash.OverlayMaterial = NewObject<UMaterial>(GetTransientPackage());

    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.WorldComp.Channel")));
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

    AActor* Attacker = World->SpawnActor<AActor>();
    AActor* Target = World->SpawnActor<AActor>();
    UStaticMeshComponent* TargetMesh = NewObject<UStaticMeshComponent>(Target);
    Target->SetRootComponent(TargetMesh);
    TargetMesh->RegisterComponent();

    WorldComp->BeginPlay();

    FGameFeedbackSignal Signal;
    Signal.PrimaryChannel = Channel;
    Signal.Instigator = Attacker;
    Signal.Target = Target;
    Signal.HitResult.ImpactPoint = FVector(100.0f, 0.0f, 0.0f);
    Signal.HitResult.Component = TargetMesh;
    Signal.Magnitude = 10.0f;

    FGameFeedbackMessage PlayMsg;
    PlayMsg.Signal = Signal;
    PlayMsg.OverrideDefinition = Definition;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Play, PlayMsg);
    TestTrue(TEXT("Play 消息必须对目标应用闪白材质"), TargetMesh->GetOverlayMaterial() == Definition->HitFlash.OverlayMaterial.Get());

    FGameFeedbackClearMessage ClearMsg;
    ClearMsg.Target = Target;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Clear, ClearMsg);
    TestNull(TEXT("Clear 消息必须清除目标闪白材质"), TargetMesh->GetOverlayMaterial());

    ++GFrameCounter;
    FGameFeedbackNetworkMessage NetMsg;
    NetMsg.Signal = Signal;
    NetMsg.ResolvedDefinition = Definition;
    NetMsg.bSuppressed = true;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Network, NetMsg);
    TestNull(TEXT("抑制的网络消息不得应用表现"), TargetMesh->GetOverlayMaterial());

    NetMsg.bSuppressed = false;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Network, NetMsg);
    TestTrue(TEXT("合法网络消息必须应用表现"), TargetMesh->GetOverlayMaterial() == Definition->HitFlash.OverlayMaterial.Get());

    TargetMesh->SetOverlayMaterial(nullptr);
    Bus->BroadcastMessage(TAG_Feedback_Channel_Network, NetMsg);
    TestNull(TEXT("同帧完全相同的网络消息必须被去重抑制"), TargetMesh->GetOverlayMaterial());

    ++GFrameCounter;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Network, NetMsg);
    TestTrue(TEXT("新帧下相同网络消息重新放行"), TargetMesh->GetOverlayMaterial() == Definition->HitFlash.OverlayMaterial.Get());

    WorldComp->EndPlay(EEndPlayReason::Destroyed);
    TestNull(TEXT("EndPlay 后闪白被完全还原并注销"), TargetMesh->GetOverlayMaterial());

    Settings->GlobalRegistry = PrevRegistry;
    GameInstance->Shutdown();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
