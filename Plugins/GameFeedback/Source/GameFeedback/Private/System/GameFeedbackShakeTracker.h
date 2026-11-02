/**
 * @file GameFeedbackShakeTracker.h
 * @brief 本地镜头震动实例追踪（按 CameraManager 隔离，支持分屏）
 */

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"

class APlayerCameraManager;

/**
 * @brief 按 PlayerCameraManager 独立维护同帧最大振幅震动实例
 */
class FGameFeedbackShakeTracker {
public:
    FGameFeedbackShakeTracker() = default;

    /**
     * @brief 启动或替换指定摄像机管理器上的镜头震动
     * @param CameraManager 本地摄像机管理器
     * @param ShakeClass 震动类
     * @param ShakeScale 震动强度
     * @param LocalImpactDirection 相对摄像机朝向的局部冲击方向
     * @param bOrientToImpact 是否启用方向性震屏
     */
    void Execute(
        APlayerCameraManager* CameraManager,
        TSubclassOf<UCameraShakeBase> ShakeClass,
        float ShakeScale,
        const FVector& LocalImpactDirection,
        bool bOrientToImpact);

    /**
     * @brief 停止全部已启动震动并清理失效摄像机记录
     */
    void StopAll();

private:
    struct FState {
        TWeakObjectPtr<UCameraShakeBase> Instance = nullptr;
        uint64 LastFrame = 0;
        float MaxScale = 0.0f;
    };

    TMap<TWeakObjectPtr<APlayerCameraManager>, FState> States;
};
