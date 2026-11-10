/**
 * @file FGameDebugDraw.cpp
 * @brief 现代化轻量 3D 调试图形绘制工具实现
 */

#include "Visualizer/FGameDebugDraw.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"

void FGameDebugDraw::DrawLine(
    const UWorld* World,
    const FVector& LineStart,
    const FVector& LineEnd,
    const FColor& Color,
    float Duration,
    float Thickness) {
#if !UE_BUILD_SHIPPING
    if (World != nullptr) {
        DrawDebugLine(
            World,
            LineStart,
            LineEnd,
            Color,
            false,
            Duration,
            0,
            Thickness
        );
    }
#endif
}

void FGameDebugDraw::DrawSphere(
    const UWorld* World,
    const FVector& Center,
    float Radius,
    const FColor& Color,
    int32 Segments,
    float Duration,
    float Thickness) {
#if !UE_BUILD_SHIPPING
    if (World != nullptr && Radius > 0.0f) {
        DrawDebugSphere(
            World,
            Center,
            Radius,
            Segments,
            Color,
            false,
            Duration,
            0,
            Thickness
        );
    }
#endif
}

void FGameDebugDraw::DrawBox(
    const UWorld* World,
    const FVector& Center,
    const FVector& Extent,
    const FQuat& Rotation,
    const FColor& Color,
    float Duration,
    float Thickness) {
#if !UE_BUILD_SHIPPING
    if (World != nullptr && Extent.X >= 0.0f && Extent.Y >= 0.0f && Extent.Z >= 0.0f) {
        DrawDebugBox(
            World,
            Center,
            Extent,
            Rotation,
            Color,
            false,
            Duration,
            0,
            Thickness
        );
    }
#endif
}
