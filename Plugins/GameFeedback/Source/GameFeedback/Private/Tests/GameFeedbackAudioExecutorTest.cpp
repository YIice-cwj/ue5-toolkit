/**
 * @file GameFeedbackAudioExecutorTest.cpp
 * @brief 音效执行器空间化、确认音阈值门禁、去重异步预热与 DS 短路自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Execution/GameFeedbackAudioExecutor.h"
#include "Feedback/GameFeedbackActions.h"
#include "Feedback/GameFeedbackSignal.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundWave.h"
#include "System/GameFeedbackDefinitionProvider.h"
#include "Tests/GameFeedbackTestHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackAudioExecutorTest,
    "Game.Feedback.AudioExecutor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证打击音效与确认音的阈值门禁、软引用去重预热托管及同步播放契约
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackAudioExecutorTest::RunTest([[maybe_unused]] const FString& Parameters) {
    GameFeedbackTestHelpers::FScopedLightweightGameWorld WorldGuard;
    UWorld* World = WorldGuard.GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UGameFeedbackDefinitionProvider* Provider = World->GetSubsystem<UGameFeedbackDefinitionProvider>();
    TestNotNull(TEXT("游戏世界应自动创建 DefinitionProvider 托管预热句柄"), Provider);
    if (!Provider) {
        return false;
    }

    AActor* Target = World->SpawnActor<AActor>();
    Target->SetActorLocation(FVector(200.0f, 0.0f, 0.0f));

    FGameFeedbackSignal Signal;
    Signal.Magnitude = 10.0f;
    Signal.Target = Target;
    Signal.HitResult.ImpactPoint = FVector(100.0f, 0.0f, 0.0f);

    FGameFeedbackAudioAction Action;
    Action.MinMagnitude = 5.0f;

    TestFalse(TEXT("空世界时空间音效必须返回 false"), GameFeedback::Execution::PlayImpactAudio(nullptr, Signal, Action));

    Signal.Magnitude = 1.0f;
    TestFalse(TEXT("低于最小强度门禁时必须拦截并返回 false"), GameFeedback::Execution::PlayImpactAudio(World, Signal, Action));

    Signal.Magnitude = 10.0f;
    TestFalse(TEXT("未配置音效软指针时必须拦截并返回 false"), GameFeedback::Execution::PlayImpactAudio(World, Signal, Action));
    TestEqual(TEXT("空软指针不得产生预热句柄"), Provider->GetActivePreloadHandleCount(), 0);

    Action.SpatialImpactSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Feedback/Test/NonExistentSound.NonExistentSound")));
    TestFalse(TEXT("未驻留软引用触发预热并跳过本次播放"), GameFeedback::Execution::PlayImpactAudio(World, Signal, Action));
    const int32 HandlesAfterFirstMiss = Provider->GetActivePreloadHandleCount();
    TestFalse(TEXT("同路径重复触发预热必须被去重"), GameFeedback::Execution::PlayImpactAudio(World, Signal, Action));
    TestEqual(TEXT("重复未命中绝不得新增泄漏句柄"), Provider->GetActivePreloadHandleCount(), HandlesAfterFirstMiss);

    USoundWave* Sound = NewObject<USoundWave>(GetTransientPackage());
    Action.SpatialImpactSound = Sound;
    TestTrue(TEXT("已驻留音效对象在有效命中点必须同步派发并返回 true"), GameFeedback::Execution::PlayImpactAudio(World, Signal, Action));

    Signal.HitResult.ImpactPoint = FVector::ZeroVector;
    TestTrue(TEXT("无命中点时回退目标位置仍须成功派发"), GameFeedback::Execution::PlayImpactAudio(World, Signal, Action));

    Signal.Target = nullptr;
    TestTrue(TEXT("无目标时回退零点仍须安全派发"), GameFeedback::Execution::PlayImpactAudio(World, Signal, Action));

    FGameFeedbackAudioAction ConfirmAction;
    ConfirmAction.MinMagnitude = 5.0f;
    TestFalse(TEXT("空世界时确认音必须返回 false"), GameFeedback::Execution::PlayAttackerConfirmation(nullptr, Signal, ConfirmAction));

    Signal.Magnitude = 2.0f;
    TestFalse(TEXT("低于确认音最小强度门禁必须返回 false"), GameFeedback::Execution::PlayAttackerConfirmation(World, Signal, ConfirmAction));

    Signal.Magnitude = 10.0f;
    TestFalse(TEXT("未配置确认音软指针必须返回 false"), GameFeedback::Execution::PlayAttackerConfirmation(World, Signal, ConfirmAction));

    ConfirmAction.AttackerConfirmationSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Feedback/Test/NonExistentConfirm.NonExistentConfirm")));
    TestFalse(TEXT("未驻留确认音软引用进入异步预热返回 false"), GameFeedback::Execution::PlayAttackerConfirmation(World, Signal, ConfirmAction));

    ConfirmAction.AttackerConfirmationSound = Sound;
    TestTrue(TEXT("已驻留确认音对象必须同步派发并返回 true"), GameFeedback::Execution::PlayAttackerConfirmation(World, Signal, ConfirmAction));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackAudioDedicatedServerTest,
    "Game.Feedback.AudioDedicatedServer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Dedicated Server 环境下音效执行器直接短路退出且不触发异步预热
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackAudioDedicatedServerTest::RunTest([[maybe_unused]] const FString& Parameters) {
    GameFeedbackTestHelpers::FScopedLightweightGameWorld WorldGuard;
    UWorld* World = WorldGuard.GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    World->WorldType = EWorldType::PIE;
    World->SetPlayInEditorInitialNetMode(NM_DedicatedServer);
    TestEqual(TEXT("测试世界必须处于 DedicatedServer 模式"), World->GetNetMode(), NM_DedicatedServer);
    UGameFeedbackDefinitionProvider* Provider = World->GetSubsystem<UGameFeedbackDefinitionProvider>();
    TestNotNull(TEXT("DS 世界 Provider 有效"), Provider);

    FGameFeedbackSignal Signal;
    Signal.Magnitude = 10.0f;

    FGameFeedbackAudioAction Action;
    Action.MinMagnitude = 1.0f;
    USoundWave* Sound = NewObject<USoundWave>(GetTransientPackage());
    Action.SpatialImpactSound = Sound;
    Action.AttackerConfirmationSound = Sound;

    TestFalse(TEXT("DS 环境空间音必须直接短路返回 false"), GameFeedback::Execution::PlayImpactAudio(World, Signal, Action));
    TestFalse(TEXT("DS 环境确认音必须直接短路返回 false"), GameFeedback::Execution::PlayAttackerConfirmation(World, Signal, Action));

    Action.SpatialImpactSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Feedback/Test/DSSound.DSSound")));
    TestFalse(TEXT("DS 环境软引用音效不得触发预热"), GameFeedback::Execution::PlayImpactAudio(World, Signal, Action));
    if (Provider) {
        TestEqual(TEXT("DS 短路不得注册任何媒体预热句柄"), Provider->GetActivePreloadHandleCount(), 0);
    }

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
