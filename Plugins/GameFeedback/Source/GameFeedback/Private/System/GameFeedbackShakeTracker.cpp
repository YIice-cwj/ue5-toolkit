// Copyright Epic Games, Inc. All Rights Reserved.

#include "System/GameFeedbackShakeTracker.h"

#include "Camera/PlayerCameraManager.h"

/**
 * @brief 启动或替换指定摄像机管理器上的镜头震动
 * @param CameraManager 本地摄像机管理器
 * @param ShakeClass 震动类
 * @param ShakeScale 震动强度
 * @param LocalImpactDirection 相对摄像机朝向的局部冲击方向
 * @param bOrientToImpact 是否启用方向性震屏
 */
void FGameFeedbackShakeTracker::Execute(
    APlayerCameraManager* CameraManager,
    TSubclassOf<UCameraShakeBase> ShakeClass,
    float ShakeScale,
    const FVector& LocalImpactDirection,
    bool bOrientToImpact)
{
    if (!CameraManager || !ShakeClass || ShakeScale <= 0.0f) {
        return;
    }

    for (auto It = States.CreateIterator(); It; ++It) {
        if (!It.Key().IsValid()) {
            It.RemoveCurrent();
        }
    }

    const uint64 CurrentFrame = GFrameCounter;
    FState& State = States.FindOrAdd(CameraManager);

    if (State.LastFrame == CurrentFrame && ShakeScale <= State.MaxScale) {
        return;
    }

    if (State.LastFrame == CurrentFrame && State.Instance.IsValid()) {
        CameraManager->StopCameraShake(State.Instance.Get(), false);
    }

    State.LastFrame = CurrentFrame;
    State.MaxScale = ShakeScale;
    if (bOrientToImpact && !LocalImpactDirection.IsNearlyZero()) {
        State.Instance = CameraManager->StartCameraShake(
            ShakeClass,
            ShakeScale,
            ECameraShakePlaySpace::UserDefined,
            LocalImpactDirection.Rotation()
        );
    } else {
        State.Instance = CameraManager->StartCameraShake(ShakeClass, ShakeScale);
    }
}

/**
 * @brief 停止全部已启动震动并清理失效摄像机记录
 */
void FGameFeedbackShakeTracker::StopAll()
{
    for (auto It = States.CreateIterator(); It; ++It) {
        if (APlayerCameraManager* CameraManager = It.Key().Get()) {
            if (UCameraShakeBase* Shake = It.Value().Instance.Get()) {
                CameraManager->StopCameraShake(Shake, false);
            }
        }
        It.RemoveCurrent();
    }
}
