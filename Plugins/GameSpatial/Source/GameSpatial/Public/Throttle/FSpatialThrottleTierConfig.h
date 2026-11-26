/**
 * @file FSpatialThrottleTierConfig.h
 * @brief 空间视锥与距离分级节流配置参数值结构体
 */

#pragma once

#include "CoreMinimal.h"

/**
 * @brief 空间分级节流配置参数
 */
struct FSpatialThrottleTierConfig {
    /**
     * @brief 近距档位距离阈值（厘米）
     */
    float DistanceNear = 1500.0f;

    /**
     * @brief 中距档位距离阈值（厘米，不得小于 DistanceNear）
     */
    float DistanceMid = 3000.0f;

    /**
     * @brief 远距档位距离上限（厘米，超出此距离或 ViewContext.MaxDistance 即降级为 OutOfRange）
     */
    float DistanceFar = 6000.0f;

    /**
     * @brief 距离挡位切换双向迟滞带宽度（厘米，消除边界乒乓抖动）
     */
    float DistanceHysteresis = 50.0f;

    /**
     * @brief 近距及高重要度实体的更新周期（秒，0.0f 表示每帧满速）
     */
    float NearInterval = 0.0f;

    /**
     * @brief 中距档位实体的更新周期（秒，默认约 30Hz）
     */
    float MidInterval = 0.0333f;

    /**
     * @brief 远距档位实体的更新周期（秒，默认约 15Hz）
     */
    float FarInterval = 0.0666f;

    /**
     * @brief 超出最大距离或处于视锥背后实体的更新周期（秒，默认 2Hz）
     */
    float OutOfRangeOrBehindInterval = 0.5f;

    /**
     * @brief 视锥半角退出方向迟滞带宽（度数，范围 [0, 90]）
     */
    float HysteresisDegrees = 5.0f;
};
