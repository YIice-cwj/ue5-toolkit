/**
 * @file GameFeedbackFlashTrackerTest.cpp
 * @brief 材质闪白追踪器生命周期与定时还原自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "System/GameFeedbackFlashTracker.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackFlashTrackerLifecycleTest,
    "Game.Feedback.FlashTrackerLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证闪白按最大绝对到期续期、提前回调重新 arm，并在 GC 后安全还原
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackFlashTrackerLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    FTimerManager& TimerManager = World->GetTimerManager();
    FGameFeedbackFlashTracker Tracker;
    double SimulatedTime = 1000.0;
    Tracker.SetTimeProvider([&SimulatedTime]() { return SimulatedTime; });

    AActor* MaxTarget = World->SpawnActor<AActor>();
    UStaticMeshComponent* MaxMesh = NewObject<UStaticMeshComponent>(MaxTarget);
    MaxTarget->SetRootComponent(MaxMesh);
    MaxMesh->RegisterComponent();
    UMaterial* OriginalMaterial = NewObject<UMaterial>(GetTransientPackage());
    UMaterial* FlashMaterial = NewObject<UMaterial>(GetTransientPackage());
    MaxMesh->SetOverlayMaterial(OriginalMaterial);

    FGameFeedbackHitFlashAction Action;
    Action.TargetMode = EGameFeedbackFlashTargetMode::HitComponentOnly;
    Action.OverlayMaterial = FlashMaterial;
    Action.Duration = 10.0f;
    FGameFeedbackHitFlashAction ShorterAction = Action;
    ShorterAction.Duration = 0.001f;
    FHitResult HitResult;
    HitResult.Component = MaxMesh;

    const double MaxStartRealTime = SimulatedTime;
    Tracker.Execute(World, MaxTarget, HitResult, Action, 10.0f);
    Tracker.Execute(World, MaxTarget, HitResult, ShorterAction, 10.0f);
    TWeakObjectPtr<UMaterialInterface> OriginalWeak = OriginalMaterial;
    OriginalMaterial = nullptr;
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("仅由 Tracker 持有的原 Overlay 必须经 GC 存活"), OriginalWeak.IsValid());
    SimulatedTime += 2.0;
    TestTrue(TEXT("最大到期用例必须越过较短 Duration"), SimulatedTime >= MaxStartRealTime + ShorterAction.Duration);
    TestTrue(TEXT("提前回调必须发生在最大绝对到期之前"), SimulatedTime < MaxStartRealTime + Action.Duration);

    ++GFrameCounter;
    TimerManager.Tick(0.0f);
    ++GFrameCounter;
    TimerManager.Tick(2.0f);
    TestTrue(TEXT("较短 Duration 不得覆盖最大绝对到期"), MaxMesh->GetOverlayMaterial() == FlashMaterial);

    Tracker.ClearActor(World, MaxTarget);
    TestEqual(TEXT("ClearActor 必须还原 GC 后仍存活的原 Overlay"), MaxMesh->GetOverlayMaterial(), OriginalWeak.Get());
    Tracker.Execute(World, MaxTarget, HitResult, Action, 10.0f);
    Tracker.Shutdown(World);
    TestEqual(TEXT("Shutdown 必须还原 Overlay"), MaxMesh->GetOverlayMaterial(), OriginalWeak.Get());

    AActor* RearmTarget = World->SpawnActor<AActor>();
    UStaticMeshComponent* RearmMesh = NewObject<UStaticMeshComponent>(RearmTarget);
    RearmTarget->SetRootComponent(RearmMesh);
    RearmMesh->RegisterComponent();
    HitResult.Component = RearmMesh;
    Action.Duration = 0.05f;
    ShorterAction.Duration = 0.001f;
    const double RearmExpirationRealTime = SimulatedTime + Action.Duration;
    Tracker.Execute(World, RearmTarget, HitResult, Action, 10.0f);
    Tracker.Execute(World, RearmTarget, HitResult, ShorterAction, 10.0f);
    TestTrue(TEXT("重新 arm 用例的首次回调必须提前发生"), SimulatedTime < RearmExpirationRealTime);

    ++GFrameCounter;
    TimerManager.Tick(0.0f);
    ++GFrameCounter;
    TimerManager.Tick(1.0f);
    TestTrue(TEXT("提前回调不得提前还原 Overlay"), RearmMesh->GetOverlayMaterial() == FlashMaterial);
    SimulatedTime = RearmExpirationRealTime + 0.1;
    TestTrue(TEXT("最终回调前必须已经越过真实到期时间"), SimulatedTime >= RearmExpirationRealTime);
    ++GFrameCounter;
    TimerManager.Tick(1.0f);
    TestNull(TEXT("提前回调后重新 arm 必须在最终到期时还原"), RearmMesh->GetOverlayMaterial());

    Tracker.Shutdown(World);
    World->DestroyWorld(false);
    World->RemoveFromRoot();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
