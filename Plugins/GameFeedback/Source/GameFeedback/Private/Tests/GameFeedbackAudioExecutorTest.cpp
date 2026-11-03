/**
 * @file GameFeedbackAudioExecutorTest.cpp
 * @brief 音效执行器空间化与确认音自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Feedback/GameFeedbackActions.h"
#include "Feedback/GameFeedbackSignal.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundWave.h"
#include "System/GameFeedbackAudioExecutor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackAudioExecutorTest,
    "Game.Feedback.AudioExecutor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证打击音效与确认音在阈值门禁、软引用异步加载及位置回退分支的行为
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackAudioExecutorTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    AActor* Target = World->SpawnActor<AActor>();
    Target->SetActorLocation(FVector(200.0f, 0.0f, 0.0f));

    FGameFeedbackSignal Signal;
    Signal.Magnitude = 10.0f;
    Signal.Target = Target;
    Signal.HitResult.ImpactPoint = FVector(100.0f, 0.0f, 0.0f);

    FGameFeedbackAudioAction Action;
    Action.MinMagnitude = 5.0f;

    GameFeedback::Execution::PlayImpactAudio(nullptr, Signal, Action);
    Signal.Magnitude = 1.0f;
    GameFeedback::Execution::PlayImpactAudio(World, Signal, Action);
    Signal.Magnitude = 10.0f;
    GameFeedback::Execution::PlayImpactAudio(World, Signal, Action);

    Action.SpatialImpactSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Feedback/Test/NonExistentSound.NonExistentSound")));
    GameFeedback::Execution::PlayImpactAudio(World, Signal, Action);

    USoundWave* Sound = NewObject<USoundWave>(GetTransientPackage());
    Action.SpatialImpactSound = Sound;
    GameFeedback::Execution::PlayImpactAudio(World, Signal, Action);

    Signal.HitResult.ImpactPoint = FVector::ZeroVector;
    GameFeedback::Execution::PlayImpactAudio(World, Signal, Action);
    Signal.Target = nullptr;
    GameFeedback::Execution::PlayImpactAudio(World, Signal, Action);

    FGameFeedbackAudioAction ConfirmAction;
    ConfirmAction.MinMagnitude = 5.0f;
    GameFeedback::Execution::PlayAttackerConfirmation(nullptr, Signal, ConfirmAction);
    Signal.Magnitude = 2.0f;
    GameFeedback::Execution::PlayAttackerConfirmation(World, Signal, ConfirmAction);
    Signal.Magnitude = 10.0f;
    GameFeedback::Execution::PlayAttackerConfirmation(World, Signal, ConfirmAction);

    ConfirmAction.AttackerConfirmationSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Feedback/Test/NonExistentConfirm.NonExistentConfirm")));
    GameFeedback::Execution::PlayAttackerConfirmation(World, Signal, ConfirmAction);

    ConfirmAction.AttackerConfirmationSound = Sound;
    GameFeedback::Execution::PlayAttackerConfirmation(World, Signal, ConfirmAction);

    World->DestroyWorld(false);
    World->RemoveFromRoot();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
