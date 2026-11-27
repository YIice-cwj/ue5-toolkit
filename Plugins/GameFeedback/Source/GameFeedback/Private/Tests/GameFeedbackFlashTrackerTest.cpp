/**
 * @file GameFeedbackFlashTrackerTest.cpp
 * @brief 材质闪白追踪器生命周期、定时还原与析构防 UAF 自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Tests/GameFeedbackTestHelpers.h"
#include "TimerManager.h"
#include "Tracker/GameFeedbackFlashTracker.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackFlashTrackerLifecycleTest,
    "Game.Feedback.FlashTrackerLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证闪白按最大绝对到期续期、提前回调重新 arm、GC 强保活与析构防 UAF 安全还原
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackFlashTrackerLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    GameFeedbackTestHelpers::FScopedLightweightGameWorld WorldGuard;
    UWorld* World = WorldGuard.GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

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
    Action.MinMagnitude = 5.0f;
    FHitResult HitResult;
    HitResult.Component = MaxMesh;

    Tracker.Execute(World, MaxTarget, HitResult, Action, 1.0f);
    TestEqual(TEXT("低于 MinMagnitude 门禁不应追踪或改写材质"), Tracker.GetTrackedCount(), 0);
    TestEqual(TEXT("低于门禁时原 Overlay 保持不变"), MaxMesh->GetOverlayMaterial(), static_cast<UMaterialInterface*>(OriginalMaterial));

    Action.bEnable = false;
    Tracker.Execute(World, MaxTarget, HitResult, Action, 10.0f);
    TestEqual(TEXT("bEnable 为 false 时不应追踪"), Tracker.GetTrackedCount(), 0);
    Action.bEnable = true;

    FGameFeedbackHitFlashAction ShorterAction = Action;
    ShorterAction.Duration = 0.001f;

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

    RearmMesh->SetOverlayMaterial(OriginalWeak.Get());
    Action.Duration = 0.1f;
    Tracker.Execute(World, RearmTarget, HitResult, Action, 10.0f);
    SimulatedTime += 0.101;
    Tracker.Execute(World, RearmTarget, HitResult, Action, 10.0f);
    SimulatedTime += 0.2;
    ++GFrameCounter;
    TimerManager.Tick(1.0f);
    TestEqual(TEXT("临界多段打击到期后仍应还原为真正的原材质而非 Flash 材质"), RearmMesh->GetOverlayMaterial(), OriginalWeak.Get());

    {
        FGameFeedbackFlashTracker ScopedTracker;
        ScopedTracker.Execute(World, RearmTarget, HitResult, Action, 10.0f);
    }
    ++GFrameCounter;
    TimerManager.Tick(1.0f);
    TestEqual(TEXT("Tracker 析构后自动兜底还原 Overlay 且定时器推进零 UAF"), RearmMesh->GetOverlayMaterial(), OriginalWeak.Get());

    Tracker.Shutdown(World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackFlashTrackerPruneDeadMeshesTest,
    "Game.Feedback.FlashTrackerPruneDeadMeshes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证网格体异常销毁后，FlashTracker 能通过 PruneInvalid 自动清理失效弱引用、定时器并释放材质强引用
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackFlashTrackerPruneDeadMeshesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    GameFeedbackTestHelpers::FScopedLightweightGameWorld WorldGuard;
    UWorld* World = WorldGuard.GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    FGameFeedbackFlashTracker Tracker;

    AActor* Target = World->SpawnActor<AActor>();
    UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Target);
    Target->SetRootComponent(Mesh);
    Mesh->RegisterComponent();

    UMaterial* OriginalMaterial = NewObject<UMaterial>(GetTransientPackage());
    UMaterial* FlashMaterial = NewObject<UMaterial>(GetTransientPackage());
    Mesh->SetOverlayMaterial(OriginalMaterial);

    FGameFeedbackHitFlashAction Action;
    Action.TargetMode = EGameFeedbackFlashTargetMode::HitComponentOnly;
    Action.OverlayMaterial = FlashMaterial;
    Action.Duration = 10.0f;
    FHitResult HitResult;
    HitResult.Component = Mesh;

    Tracker.Execute(World, Target, HitResult, Action, 10.0f);
    TestEqual(TEXT("追踪器初始应持有 1 条网格记录"), Tracker.GetTrackedCount(), 1);

    Target->Destroy();
    Target = nullptr;
    Mesh = nullptr;

    Tracker.PruneInvalid(World);
    TestEqual(TEXT("网格销毁后调用 PruneInvalid 应成功修剪失效记录"), Tracker.GetTrackedCount(), 0);

    TArray<AActor*> DeadActors;
    DeadActors.Reserve(24);
    for (int32 Index = 0; Index < 24; ++Index) {
        AActor* DeadActor = World->SpawnActor<AActor>();
        UStaticMeshComponent* DeadMesh = NewObject<UStaticMeshComponent>(DeadActor);
        DeadActor->SetRootComponent(DeadMesh);
        DeadMesh->RegisterComponent();
        HitResult.Component = DeadMesh;
        Action.Duration = (Index < 12) ? 0.05f : 10.0f;
        Tracker.Execute(World, DeadActor, HitResult, Action, 10.0f);
        DeadActors.Add(DeadActor);
    }
    TestEqual(TEXT("批量注册 24 条网格追踪记录"), Tracker.GetTrackedCount(), 24);
    for (AActor* DeadActor : DeadActors) {
        DeadActor->Destroy();
    }
    ++GFrameCounter;
    World->GetTimerManager().Tick(0.0f);
    ++GFrameCounter;
    World->GetTimerManager().Tick(0.1f);
    TestEqual(TEXT("前半批失效网格到期回调仅移除自身 12 条，绝不误删后半批失效键"), Tracker.GetTrackedCount(), 12);
    Tracker.PruneInvalid(World);
    TestEqual(TEXT("PruneInvalid 清理剩余全部 12 条失效记录"), Tracker.GetTrackedCount(), 0);

    Tracker.Shutdown(World);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
