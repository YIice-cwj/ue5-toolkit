/**
 * @file FGameDebugDraw.cpp
 * @brief 现代化轻量 3D 调试图形绘制工具实现
 */

#include "Visualizer/FGameDebugDraw.h"

#include "Config/GameCompileSwitches.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace {

[[nodiscard]] FORCEINLINE bool IsPositiveFinite(float Value) noexcept {
    return FMath::IsFinite(Value) && Value > 0.0f;
}

[[nodiscard]] FORCEINLINE bool IsValidStrokeStyle(float Duration, float Thickness) noexcept {
    return FMath::IsFinite(Duration) && Duration >= 0.0f && FMath::IsFinite(Thickness) && Thickness >= 0.0f;
}

}  // namespace

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
    }

    if (World == nullptr || LineStart.ContainsNaN() || LineEnd.ContainsNaN() || !IsValidStrokeStyle(Duration, Thickness)) {
        return;
    }

    DrawDebugLine(World, LineStart, LineEnd, Color, false, Duration, 0, Thickness);
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
    }

    if (World == nullptr || Center.ContainsNaN() || !IsPositiveFinite(Radius) || Segments <= 0 || !IsValidStrokeStyle(Duration, Thickness)) {
        return;
    }

    DrawDebugSphere(World, Center, Radius, Segments, Color, false, Duration, 0, Thickness);
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
    }

    const bool bHasInvalidVector = Center.ContainsNaN() || Extent.ContainsNaN() || Rotation.ContainsNaN();
    const bool bHasValidExtent = Extent.X >= 0.0 && Extent.Y >= 0.0 && Extent.Z >= 0.0 && !Extent.IsNearlyZero();
    if (World == nullptr || bHasInvalidVector || !bHasValidExtent || !IsValidStrokeStyle(Duration, Thickness)) {
        return;
    }

    DrawDebugBox(World, Center, Extent, Rotation.GetNormalized(), Color, false, Duration, 0, Thickness);
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
    }

    const bool bHasInvalidTransform = Center.ContainsNaN() || Rotation.ContainsNaN();
    if (World == nullptr || bHasInvalidTransform || !IsPositiveFinite(HalfHeight) || !IsPositiveFinite(Radius) || !IsValidStrokeStyle(Duration, Thickness)) {
        return;
    }

    DrawDebugCapsule(World, Center, HalfHeight, Radius, Rotation.GetNormalized(), Color, false, Duration, 0, Thickness);
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
    }

    const bool bInvalidEndpoints = Start.ContainsNaN() || End.ContainsNaN() || Start.Equals(End);
    if (World == nullptr || bInvalidEndpoints || !IsPositiveFinite(Radius) || Segments <= 0 || !IsValidStrokeStyle(Duration, Thickness)) {
        return;
    }

    DrawDebugCylinder(World, Start, End, Radius, Segments, Color, false, Duration, 0, Thickness);
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
    }

    const bool bInvalidGeometry = Origin.ContainsNaN() || Direction.ContainsNaN() || Direction.IsNearlyZero();
    const bool bInvalidAngles = !IsPositiveFinite(AngleWidthRad) || !IsPositiveFinite(AngleHeightRad) || NumSides <= 0;
    if (World == nullptr || bInvalidGeometry || !IsPositiveFinite(Length) || bInvalidAngles || !IsValidStrokeStyle(Duration, Thickness)) {
        return;
    }

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
    }

    const bool bInvalidEndpoints = LineStart.ContainsNaN() || LineEnd.ContainsNaN() || LineStart.Equals(LineEnd);
    if (World == nullptr || bInvalidEndpoints || !IsPositiveFinite(ArrowSize) || !IsValidStrokeStyle(Duration, Thickness)) {
        return;
    }

    DrawDebugDirectionalArrow(World, LineStart, LineEnd, ArrowSize, Color, false, Duration, 0, Thickness);
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
    }

    const bool bInvalidTransform = AxisLoc.ContainsNaN() || AxisRot.ContainsNaN();
    if (World == nullptr || bInvalidTransform || !IsPositiveFinite(Scale) || !IsValidStrokeStyle(Duration, Thickness)) {
        return;
    }

    DrawDebugCoordinateSystem(World, AxisLoc, AxisRot, Scale, false, Duration, 0, Thickness);
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
    }

    const bool bInvalidStyle = !IsPositiveFinite(FontScale) || !FMath::IsFinite(Duration) || Duration < 0.0f;
    const bool bInvalidActor = (TestActor != nullptr) && (!IsValid(TestActor) || TestActor->GetWorld() != World);
    if (World == nullptr || TextLocation.ContainsNaN() || Text.IsEmpty() || bInvalidStyle || bInvalidActor) {
        return;
    }

    DrawDebugString(World, TextLocation, Text, TestActor, TextColor, Duration, bDrawShadow, FontScale);
}
