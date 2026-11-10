/**
 * @file FGameDebugDraw.h
 * @brief 现代化轻量 3D 调试图形绘制工具（安全判空防崩溃与 Shipping 构建零开销剥离）
 */

#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * @brief 3D 调试图形绘制工具类
 */
class GAMEDEBUG_API FGameDebugDraw {
public:
    /**
     * @brief 绘制调试线段
     * @param World 目标世界
     * @param LineStart 起点
     * @param LineEnd 终点
     * @param Color 线段颜色
     * @param Duration 持续时间（0 为单帧）
     * @param Thickness 线条粗细
     */
    static void DrawLine(
        const UWorld* World,
        const FVector& LineStart,
        const FVector& LineEnd,
        const FColor& Color = FColor::Green,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );

    /**
     * @brief 绘制调试球体
     * @param World 目标世界
     * @param Center 球心坐标
     * @param Radius 球体半径
     * @param Color 线框颜色
     * @param Segments 细分段数
     * @param Duration 持续时间（0 为单帧）
     * @param Thickness 线条粗细
     */
    static void DrawSphere(
        const UWorld* World,
        const FVector& Center,
        float Radius,
        const FColor& Color = FColor::Cyan,
        int32 Segments = 16,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );

    /**
     * @brief 绘制调试轴对齐或定向包围盒
     * @param World 目标世界
     * @param Center 中心坐标
     * @param Extent 盒体半长宽（半尺寸）
     * @param Rotation 盒体朝向
     * @param Color 线框颜色
     * @param Duration 持续时间（0 为单帧）
     * @param Thickness 线条粗细
     */
    static void DrawBox(
        const UWorld* World,
        const FVector& Center,
        const FVector& Extent,
        const FQuat& Rotation = FQuat::Identity,
        const FColor& Color = FColor::Yellow,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );
};
