/**
 * @file FSpatialViewContext.h
 * @brief 摄像机/观察者空间视锥观察上下文值结构体
 */

#pragma once

#include "CoreMinimal.h"

/**
 * @brief 摄像机/观察者空间观察上下文
 */
struct FSpatialViewContext {
    /**
     * @brief 观察者世界坐标
     */
    FVector ViewLocation = FVector::ZeroVector;

    /**
     * @brief 观察者前向朝向向量（零向量视为无效视锥）
     */
    FVector ViewForward = FVector::ForwardVector;

    /**
     * @brief 观察者视锥全张角（度数，有效范围 (0, 360]，非正或非有限值视为无效视锥）
     */
    float FOVDegrees = 90.0f;

    /**
     * @brief 观察者最大视距上限（与梯级配置 DistanceFar 取较小值生效）
     */
    float MaxDistance = 10000.0f;

    /**
     * @brief 默认构造函数
     */
    FSpatialViewContext() = default;

    /**
     * @brief 完整参数构造函数
     * @param InLocation 观察者世界坐标
     * @param InForward 观察者前向朝向
     * @param InFov 观察者视锥全张角（度数）
     * @param InMaxDist 观察者最大视距上限
     */
    FSpatialViewContext(
        const FVector& InLocation,
        const FVector& InForward,
        float InFov = 90.0f,
        float InMaxDist = 10000.0f
    )
        : ViewLocation(InLocation)
        , ViewForward(InForward)
        , FOVDegrees(InFov)
        , MaxDistance(InMaxDist) {
    }
};
