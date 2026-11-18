/**
 * @file FGameDebugDraw.cpp
 * @brief 现代化轻量 3D 调试图形绘制工具实现
 */

#include "Visualizer/FGameDebugDraw.h"

#include "Config/GameCompileSwitches.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

void FGameDebugDraw::DrawLine(
    const UWorld* World,
    const FVector& LineStart,
    const FVector& LineEnd,
    const FColor& Color,
    float Duration,
    float Thickness
) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    } else {
        if (World != nullptr) {
            DrawDebugLine(World, LineStart, LineEnd, Color, false, Duration, 0, Thickness);
        }
    }
}

void FGameDebugDraw::DrawSphere(
    const UWorld* World,
    const FVector& Center,
    float Radius,
    const FColor& Color,
    int32 Segments,
    float Duration,
    float Thickness
) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    } else {
        if (World != nullptr && Radius > 0.0f) {
            DrawDebugSphere(World, Center, Radius, Segments, Color, false, Duration, 0, Thickness);
        }
    }
}

void FGameDebugDraw::DrawBox(
    const UWorld* World,
    const FVector& Center,
    const FVector& Extent,
    const FQuat& Rotation,
    const FColor& Color,
    float Duration,
    float Thickness
) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    } else {
        if (World != nullptr && Extent.X >= 0.0f && Extent.Y >= 0.0f && Extent.Z >= 0.0f) {
            DrawDebugBox(World, Center, Extent, Rotation, Color, false, Duration, 0, Thickness);
        }
    }
}

void FGameDebugDraw::DrawCapsule(
    const UWorld* World,
    const FVector& Center,
    float HalfHeight,
    float Radius,
    const FQuat& Rotation,
    const FColor& Color,
    float Duration,
    float Thickness
) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    } else {
        if (World != nullptr && HalfHeight > 0.0f && Radius > 0.0f) {
            DrawDebugCapsule(World, Center, HalfHeight, Radius, Rotation, Color, false, Duration, 0, Thickness);
        }
    }
}

void FGameDebugDraw::DrawCylinder(
    const UWorld* World,
    const FVector& Start,
    const FVector& End,
    float Radius,
    int32 Segments,
    const FColor& Color,
    float Duration,
    float Thickness
) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    } else {
        if (World != nullptr && Radius > 0.0f && !Start.Equals(End)) {
            DrawDebugCylinder(World, Start, End, Radius, Segments, Color, false, Duration, 0, Thickness);
        }
    }
}

void FGameDebugDraw::DrawCone(
    const UWorld* World,
    const FVector& Origin,
    const FVector& Direction,
    float Length,
    float AngleWidthRad,
    float AngleHeightRad,
    int32 NumSides,
    const FColor& Color,
    float Duration,
    float Thickness
) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    } else {
        if (World != nullptr && Length > 0.0f && !Direction.IsNearlyZero()) {
            DrawDebugCone(
                World,
                Origin,
                Direction.GetSafeNormal(),
                Length,
                AngleWidthRad,
                AngleHeightRad,
                NumSides,
                Color,
                false,
                Duration,
                0,
                Thickness
            );
        }
    }
}

void FGameDebugDraw::DrawArrow(
    const UWorld* World,
    const FVector& LineStart,
    const FVector& LineEnd,
    float ArrowSize,
    const FColor& Color,
    float Duration,
    float Thickness
) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    } else {
        if (World != nullptr && ArrowSize > 0.0f) {
            DrawDebugDirectionalArrow(World, LineStart, LineEnd, ArrowSize, Color, false, Duration, 0, Thickness);
        }
    }
}

void FGameDebugDraw::DrawCoordinateSystem(
    const UWorld* World,
    const FVector& AxisLoc,
    const FRotator& AxisRot,
    float Scale,
    float Duration,
    float Thickness
) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    } else {
        if (World != nullptr && Scale > 0.0f) {
            DrawDebugCoordinateSystem(World, AxisLoc, AxisRot, Scale, false, Duration, 0, Thickness);
        }
    }
}

void FGameDebugDraw::DrawString(
    const UWorld* World,
    const FVector& TextLocation,
    const FString& Text,
    AActor* TestActor,
    const FColor& TextColor,
    float Duration,
    bool bDrawShadow,
    float FontScale
) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    } else {
        if (World != nullptr && !Text.IsEmpty()) {
            DrawDebugString(World, TextLocation, Text, TestActor, TextColor, Duration, bDrawShadow, FontScale);
        }
    }
}
