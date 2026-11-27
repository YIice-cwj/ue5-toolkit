/**
 * @file GameFeedbackHitStopServiceTest.cpp
 * @brief 顿帧全责服务自伤保护、致死全局定格可配参数、网络抑制与清除自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Asset/GameFeedbackDefinition.h"
#include "Asset/GameFeedbackRegistry.h"
#include "Bus/GameFeedbackBlueprintLibrary.h"
#include "Bus/GameFeedbackChannels.h"
#include "Bus/GameFeedbackMessages.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagsManager.h"
#include "Message/GameplayMessageSubsystem.h"
#include "System/GameFeedbackHitStopService.h"
#include "Tests/GameFeedbackTestHelpers.h"
#include "Time/TimeDilationSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackHitStopServiceTest,
    "Game.Feedback.HitStopService",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证顿帧服务的视角分流、单机自伤禁止双重顿帧、可配致死全局定格、网络抑制与 Clear 清除
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackHitStopServiceTest::RunTest([[maybe_unused]] const FString& Parameters) {
    GameFeedbackTestHelpers::FScopedFeedbackSettingsGuard SettingsGuard;
    UGameFeedbackSettings* Settings = SettingsGuard.Get();
    Settings->bEnableFeedback = true;

    UGameFeedbackDefinition* Definition = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/HitStopService/Def.Def"));
    Definition->HitStop.bEnable = true;
    Definition->HitStop.VictimDuration = 0.08f;
    Definition->HitStop.AttackerDuration = 0.04f;
    Definition->HitStop.TimeDilation = 0.01f;
    Definition->HitStop.CooldownInterval = 0.01f;
    Definition->HitStop.MaxCumulativeDuration = 0.10f;
    Definition->HitStop.bGlobalFreezeOnLethal = true;
    Definition->HitStop.LethalGlobalFreezeDuration = 0.06f;
    Definition->HitStop.LethalGlobalFreezeDilation = 0.15f;
    Definition->HitStop.LethalGlobalFreezeBlendOut = 0.03f;

    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.HitStopService.Channel")));
    Registry->ExactChannelMap.Add(Channel, Definition);
    Registry->RebuildDefinitionIndex();
    Settings->GlobalRegistry = Registry;

    GameFeedbackTestHelpers::FScopedStandaloneFeedbackWorld TestEnv(FName(TEXT("GameFeedbackHitStopServiceTestWorld")));
    UWorld* World = TestEnv.GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UGameFeedbackHitStopService* Service = World->GetSubsystem<UGameFeedbackHitStopService>();
    UTimeDilationSubsystem* TimeSubsystem = World->GetSubsystem<UTimeDilationSubsystem>();
    UGameplayMessageSubsystem* Bus = TestEnv.GetGameInstance()->GetSubsystem<UGameplayMessageSubsystem>();
    TestNotNull(TEXT("HitStopService 有效"), Service);
    TestNotNull(TEXT("TimeDilationSubsystem 有效"), TimeSubsystem);
    TestNotNull(TEXT("消息总线有效"), Bus);
    if (!Service || !TimeSubsystem || !Bus) {
        return false;
    }

    Service->OnWorldBeginPlay(*World);

    APlayerController* LocalPC = World->SpawnActor<APlayerController>();
    ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
    LocalPC->SetPlayer(LocalPlayer);
    APawn* PlayerPawn = World->SpawnActor<APawn>();
    LocalPC->Possess(PlayerPawn);
    World->AddController(LocalPC);

    AActor* Enemy = World->SpawnActor<AActor>();
    AActor* OtherEnemy = World->SpawnActor<AActor>();

    FGameFeedbackSignal AttackSignal;
    AttackSignal.PrimaryChannel = Channel;
    AttackSignal.Instigator = PlayerPawn;
    AttackSignal.Target = Enemy;
    AttackSignal.Magnitude = 20.0f;
    UGameFeedbackBlueprintLibrary::BroadcastFeedback(World, AttackSignal);

    TestTrue(TEXT("本地玩家攻击敌方时目标应进入顿帧"), TimeSubsystem->IsActorInHitStop(Enemy));
    TestTrue(TEXT("本地玩家作为攻击者应获得卡肉顿帧"), TimeSubsystem->IsActorInHitStop(PlayerPawn));

    UGameFeedbackBlueprintLibrary::BroadcastClearFeedback(World, Enemy);
    TestFalse(TEXT("Clear 通道必须立即解除目标顿帧"), TimeSubsystem->IsActorInHitStop(Enemy));
    UGameFeedbackBlueprintLibrary::BroadcastClearFeedback(World, PlayerPawn);
    TimeSubsystem->ClearAllHitStops();

    FGameFeedbackSignal SelfHarmSignal;
    SelfHarmSignal.PrimaryChannel = Channel;
    SelfHarmSignal.Instigator = PlayerPawn;
    SelfHarmSignal.Target = PlayerPawn;
    SelfHarmSignal.Magnitude = 20.0f;
    UGameFeedbackBlueprintLibrary::BroadcastFeedback(World, SelfHarmSignal);
    TestTrue(TEXT("自伤时玩家应作为受击者进入顿帧"), TimeSubsystem->IsActorInHitStop(PlayerPawn));
    TimeSubsystem->AdvanceTime(0.085f);
    TestFalse(TEXT("自伤仅按单次受击时长到期，绝不叠加双重顿帧"), TimeSubsystem->IsActorInHitStop(PlayerPawn));

    UGameFeedbackBlueprintLibrary::BroadcastClearFeedback(World, PlayerPawn);
    AActor* OwnedTurret = World->SpawnActor<AActor>();
    OwnedTurret->SetOwner(PlayerPawn);
    FGameFeedbackSignal TurretSelfHarmSignal;
    TurretSelfHarmSignal.PrimaryChannel = Channel;
    TurretSelfHarmSignal.Instigator = OwnedTurret;
    TurretSelfHarmSignal.Target = PlayerPawn;
    TurretSelfHarmSignal.Magnitude = 20.0f;
    UGameFeedbackBlueprintLibrary::BroadcastFeedback(World, TurretSelfHarmSignal);
    TestTrue(TEXT("归属自身炮台误伤玩家时玩家应进入受击顿帧"), TimeSubsystem->IsActorInHitStop(PlayerPawn));
    TestFalse(TEXT("归属自身炮台误伤自己时炮台不得获得攻击者顿帧"), TimeSubsystem->IsActorInHitStop(OwnedTurret));
    UGameFeedbackBlueprintLibrary::BroadcastClearFeedback(World, PlayerPawn);
    TimeSubsystem->ClearAllHitStops();

    APlayerController* SecondLocalPC = World->SpawnActor<APlayerController>();
    ULocalPlayer* SecondLocalPlayer = NewObject<ULocalPlayer>(GEngine);
    SecondLocalPC->SetPlayer(SecondLocalPlayer);
    APawn* SecondPlayerPawn = World->SpawnActor<APawn>();
    SecondLocalPC->Possess(SecondPlayerPawn);
    World->AddController(SecondLocalPC);

    FGameFeedbackSignal SplitScreenPvpSignal;
    SplitScreenPvpSignal.PrimaryChannel = Channel;
    SplitScreenPvpSignal.Instigator = SecondPlayerPawn;
    SplitScreenPvpSignal.Target = PlayerPawn;
    SplitScreenPvpSignal.Magnitude = 20.0f;
    UGameFeedbackBlueprintLibrary::BroadcastFeedback(World, SplitScreenPvpSignal);
    TestTrue(TEXT("分屏 PvP 中后序本地玩家攻击前序本地玩家时受击者应进入顿帧"), TimeSubsystem->IsActorInHitStop(PlayerPawn));
    TestTrue(TEXT("分屏 PvP 中后序本地玩家作为攻击者也必须对称获得卡肉顿帧"), TimeSubsystem->IsActorInHitStop(SecondPlayerPawn));
    TimeSubsystem->ClearAllHitStops();
    SecondLocalPC->UnPossess();
    SecondLocalPC->Player = nullptr;

    FGameFeedbackSignal SuppressedNetSignal;
    SuppressedNetSignal.PrimaryChannel = Channel;
    SuppressedNetSignal.Instigator = PlayerPawn;
    SuppressedNetSignal.Target = OtherEnemy;
    SuppressedNetSignal.Magnitude = 30.0f;
    UGameFeedbackBlueprintLibrary::BroadcastNetworkFeedback(World, SuppressedNetSignal, Definition, true);
    TestFalse(TEXT("受抑制的网络回执不得触发顿帧"), TimeSubsystem->IsActorInHitStop(OtherEnemy));

    FGameFeedbackSignal LethalNetSignal = SuppressedNetSignal;
    LethalNetSignal.bLethal = true;
    UGameFeedbackBlueprintLibrary::BroadcastNetworkFeedback(World, LethalNetSignal, Definition, false);
    TestTrue(TEXT("未抑制的网络回执应触发受击顿帧"), TimeSubsystem->IsActorInHitStop(OtherEnemy));
    TestTrue(TEXT("致死打击应激活 HitStop 全局通道"), TimeSubsystem->IsChannelActive(ETimeDilationChannel::HitStop));
    TestNearlyEqual(
        TEXT("致死全局定格缩放必须采用可配置参数 LethalGlobalFreezeDilation"),
        TimeSubsystem->GetChannelTimeDilation(ETimeDilationChannel::HitStop),
        0.15f,
        0.0001f);

    TimeSubsystem->ClearAllHitStops();
    TimeSubsystem->ClearAllGlobalDilations();
    LocalPC->UnPossess();
    LocalPC->Player = nullptr;
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
