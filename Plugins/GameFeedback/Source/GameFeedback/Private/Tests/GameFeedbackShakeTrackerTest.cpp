/**
 * @file GameFeedbackShakeTrackerTest.cpp
 * @brief 镜头震动追踪器并发去重与生命周期自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "System/GameFeedbackShakeTracker.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackShakeTrackerTest,
    "Game.Feedback.ShakeTracker",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证镜头震动同帧振幅择优、方向适配、失效管理与停止
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackShakeTrackerTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    APlayerCameraManager* CameraManager = World->SpawnActor<APlayerCameraManager>();
    FGameFeedbackShakeTracker Tracker;

    Tracker.Execute(nullptr, UCameraShakeBase::StaticClass(), 1.0f, FVector::ForwardVector, false);
    Tracker.Execute(CameraManager, nullptr, 1.0f, FVector::ForwardVector, false);
    Tracker.Execute(CameraManager, UCameraShakeBase::StaticClass(), 0.0f, FVector::ForwardVector, false);
    TestEqual(TEXT("无效参数（空相机、空类、零振幅）不应记录任何追踪状态"), Tracker.GetTrackedCount(), 0);

    ++GFrameCounter;
    Tracker.Execute(CameraManager, UCameraShakeBase::StaticClass(), 1.0f, FVector::ForwardVector, false);
    TestEqual(TEXT("首次合法震动应成功追踪"), Tracker.GetTrackedCount(), 1);
    Tracker.Execute(CameraManager, UCameraShakeBase::StaticClass(), 0.5f, FVector::ForwardVector, false);
    TestEqual(TEXT("同帧较小振幅震动应被抑制不增加状态"), Tracker.GetTrackedCount(), 1);
    Tracker.Execute(CameraManager, UCameraShakeBase::StaticClass(), 2.0f, FVector::ForwardVector, false);
    TestEqual(TEXT("同帧较大振幅震动应替换实例并保持单条记录"), Tracker.GetTrackedCount(), 1);

    ++GFrameCounter;
    Tracker.Execute(CameraManager, UCameraShakeBase::StaticClass(), 1.5f, FVector(1.0f, 0.0f, 0.0f), true);
    Tracker.Execute(CameraManager, UCameraShakeBase::StaticClass(), 2.5f, FVector::ZeroVector, true);
    TestEqual(TEXT("跨帧合法震动继续保持相机管理器追踪状态"), Tracker.GetTrackedCount(), 1);

    Tracker.StopAll();
    TestEqual(TEXT("StopAll 必须清空全部震动状态"), Tracker.GetTrackedCount(), 0);

    APlayerCameraManager* TransientCamera = World->SpawnActor<APlayerCameraManager>();
    ++GFrameCounter;
    Tracker.Execute(TransientCamera, UCameraShakeBase::StaticClass(), 1.0f, FVector::ForwardVector, false);
    TestEqual(TEXT("临时相机应正常追踪"), Tracker.GetTrackedCount(), 1);
    World->EditorDestroyActor(TransientCamera, true);
    ++GFrameCounter;
    Tracker.Execute(CameraManager, UCameraShakeBase::StaticClass(), 1.0f, FVector::ForwardVector, false);
    TestEqual(TEXT("失效相机应在下次 Execute 时被自动清理释放"), Tracker.GetTrackedCount(), 1);
    Tracker.StopAll();
    TestEqual(TEXT("最终清理后状态清零"), Tracker.GetTrackedCount(), 0);

    World->DestroyWorld(false);
    World->RemoveFromRoot();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
