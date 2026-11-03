/**
 * @file GameFeedbackPerspectiveTest.cpp
 * @brief 本地观察视角判定与所有权链追溯自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Feedback/GameFeedbackPerspective.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackPerspectiveTest,
    "Game.Feedback.Perspective",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证本地观察视角判定逻辑、所有权链追溯及深度防御边界
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackPerspectiveTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    APlayerController* LocalPC = World->SpawnActor<APlayerController>();
    APawn* LocalPawn = World->SpawnActor<APawn>();
    LocalPC->Possess(LocalPawn);

    AActor* Attacker = World->SpawnActor<AActor>();
    AActor* DamageCauser = World->SpawnActor<AActor>();
    AActor* Target = World->SpawnActor<AActor>();

    TestEqual(
        TEXT("空本地控制器必须判定为旁观者"),
        GameFeedback::ResolvePerspective(nullptr, Attacker, DamageCauser, Target),
        EGameFeedbackPerspective::Bystander);

    Attacker->SetOwner(LocalPC);
    TestEqual(
        TEXT("攻击者归属本地控制器必须判定为本地攻击者"),
        GameFeedback::ResolvePerspective(LocalPC, Attacker, DamageCauser, Target),
        EGameFeedbackPerspective::LocalAttacker);

    Attacker->SetOwner(nullptr);
    DamageCauser->SetOwner(LocalPawn);
    TestEqual(
        TEXT("伤害源归属本地 Pawn 必须判定为本地攻击者"),
        GameFeedback::ResolvePerspective(LocalPC, Attacker, DamageCauser, Target),
        EGameFeedbackPerspective::LocalAttacker);

    DamageCauser->SetOwner(nullptr);
    Target->SetOwner(LocalPC);
    TestEqual(
        TEXT("目标归属本地控制器必须判定为本地受击者"),
        GameFeedback::ResolvePerspective(LocalPC, Attacker, DamageCauser, Target),
        EGameFeedbackPerspective::LocalVictim);

    Target->SetOwner(nullptr);
    TestEqual(
        TEXT("与本地控制器无关的实体必须判定为旁观者"),
        GameFeedback::ResolvePerspective(LocalPC, Attacker, DamageCauser, Target),
        EGameFeedbackPerspective::Bystander);

    AActor* DeepActor = World->SpawnActor<AActor>();
    AActor* Chain1 = World->SpawnActor<AActor>();
    AActor* Chain2 = World->SpawnActor<AActor>();
    AActor* Chain3 = World->SpawnActor<AActor>();
    AActor* Chain4 = World->SpawnActor<AActor>();
    AActor* Chain5 = World->SpawnActor<AActor>();
    AActor* Chain6 = World->SpawnActor<AActor>();

    Chain1->SetOwner(LocalPC);
    Chain2->SetOwner(Chain1);
    Chain3->SetOwner(Chain2);
    DeepActor->SetOwner(Chain3);
    TestEqual(
        TEXT("四层所有权追溯仍可识别本地攻击者"),
        GameFeedback::ResolvePerspective(LocalPC, DeepActor, nullptr, Target),
        EGameFeedbackPerspective::LocalAttacker);

    Chain1->SetOwner(LocalPC);
    Chain2->SetOwner(Chain1);
    Chain3->SetOwner(Chain2);
    Chain4->SetOwner(Chain3);
    Chain5->SetOwner(Chain4);
    Chain6->SetOwner(Chain5);
    DeepActor->SetOwner(Chain6);
    TestEqual(
        TEXT("超过五层所有权深度截断必须回退为旁观者"),
        GameFeedback::ResolvePerspective(LocalPC, DeepActor, nullptr, Target),
        EGameFeedbackPerspective::Bystander);

    LocalPC->UnPossess();
    World->DestroyWorld(false);
    World->RemoveFromRoot();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
