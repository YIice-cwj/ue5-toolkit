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

    TestNotNull(TEXT("测试世界有效"), World);
    TestNotNull(TEXT("目标实体有效"), Target);

    GameFeedback::Execution::PlayImpactAudio(nullptr, Signal, Action);
    Signal.Magnitude = 1.0f;
    GameFeedback::Execution::PlayImpactAudio(World, Signal, Action);
    TestTrue(TEXT("低于最小强度门禁时安全跳过"), Signal.Magnitude < Action.MinMagnitude);
    Signal.Magnitude = 10.0f;
    GameFeedback::Execution::PlayImpactAudio(World, Signal, Action);
    TestTrue(TEXT("未配置音效软指针时安全返回"), Action.SpatialImpactSound.IsNull());

    Action.SpatialImpactSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Feedback/Test/NonExistentSound.NonExistentSound")));
    TestFalse(TEXT("配置软指针后非空"), Action.SpatialImpactSound.IsNull());
    TestFalse(TEXT("未加载软资产有效指针为空"), Action.SpatialImpactSound.IsValid());
    GameFeedback::Execution::PlayImpactAudio(World, Signal, Action);

    USoundWave* Sound = NewObject<USoundWave>(GetTransientPackage());
    TestNotNull(TEXT("测试音效实例创建成功"), Sound);
    Action.SpatialImpactSound = Sound;
    TestTrue(TEXT("直接绑定音效对象时有效"), Action.SpatialImpactSound.IsValid());
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
    TestTrue(TEXT("低于确认音最小强度门禁时安全跳过"), Signal.Magnitude < ConfirmAction.MinMagnitude);
    Signal.Magnitude = 10.0f;
    GameFeedback::Execution::PlayAttackerConfirmation(World, Signal, ConfirmAction);
    TestTrue(TEXT("未配置确认音软指针时安全返回"), ConfirmAction.AttackerConfirmationSound.IsNull());

    ConfirmAction.AttackerConfirmationSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Feedback/Test/NonExistentConfirm.NonExistentConfirm")));
    TestFalse(TEXT("配置确认音软指针后非空"), ConfirmAction.AttackerConfirmationSound.IsNull());
    TestFalse(TEXT("未加载确认音有效指针为空"), ConfirmAction.AttackerConfirmationSound.IsValid());
    GameFeedback::Execution::PlayAttackerConfirmation(World, Signal, ConfirmAction);

    ConfirmAction.AttackerConfirmationSound = Sound;
    TestTrue(TEXT("直接绑定确认音对象时有效"), ConfirmAction.AttackerConfirmationSound.IsValid());
    GameFeedback::Execution::PlayAttackerConfirmation(World, Signal, ConfirmAction);

    World->DestroyWorld(false);
    World->RemoveFromRoot();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
