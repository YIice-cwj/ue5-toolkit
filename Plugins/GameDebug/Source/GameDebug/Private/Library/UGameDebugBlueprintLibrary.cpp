/**
 * @file UGameDebugBlueprintLibrary.cpp
 * @brief GameDebug 调试与诊断蓝图函数库实现
 */

#include "Library/UGameDebugBlueprintLibrary.h"

#include "Config/GameCompileSwitches.h"
#include "Print/FGameDebugPrint.h"
#include "Time/FGameDebugTimeController.h"
#include "Visualizer/FGameDebugDraw.h"

void UGameDebugBlueprintLibrary::PrintDebugMessage(
    const FString& Message,
    FColor Color,
    float Duration,
    int64 Key
) {
    if constexpr (Game::Config::bEnableDebugDraw) {
        FGameDebugPrint::Print(Message, Color, Duration, static_cast<uint64>(Key));
    }
}

void UGameDebugBlueprintLibrary::PrintChannelMessage(
    FName Channel,
    const FString& Message,
    FColor Color,
    float Duration
) {
    if constexpr (Game::Config::bEnableDebugDraw) {
        FGameDebugPrint::PrintChannel(Channel, Message, Color, Duration);
    }
}

void UGameDebugBlueprintLibrary::PrintWarning(const FString& Message, float Duration) {
    if constexpr (Game::Config::bEnableDebugDraw) {
        FGameDebugPrint::PrintWarning(Message, Duration);
    }
}

void UGameDebugBlueprintLibrary::PrintError(const FString& Message, float Duration) {
    if constexpr (Game::Config::bEnableDebugDraw) {
        FGameDebugPrint::PrintError(Message, Duration);
    }
}

void UGameDebugBlueprintLibrary::DrawDebugLine(
    const UObject* WorldContextObject,
    FVector LineStart,
    FVector LineEnd,
    FColor Color,
    float Duration,
    float Thickness
) {
    if constexpr (Game::Config::bEnableDebugDraw) {
        FGameDebugDraw::DrawLine(
            FGameDebugTimeController::ResolveWorld(WorldContextObject),
            LineStart,
            LineEnd,
            Color,
            Duration,
            Thickness
        );
    }
}

void UGameDebugBlueprintLibrary::DrawDebugSphere(
    const UObject* WorldContextObject,
    FVector Center,
    float Radius,
    FColor Color,
    int32 Segments,
    float Duration,
    float Thickness
) {
    if constexpr (Game::Config::bEnableDebugDraw) {
        FGameDebugDraw::DrawSphere(
            FGameDebugTimeController::ResolveWorld(WorldContextObject),
            Center,
            Radius,
            Color,
            Segments,
            Duration,
            Thickness
        );
    }
}

void UGameDebugBlueprintLibrary::DrawDebugBox(
    const UObject* WorldContextObject,
    FVector Center,
    FVector Extent,
    FRotator Rotation,
    FColor Color,
    float Duration,
    float Thickness
) {
    if constexpr (Game::Config::bEnableDebugDraw) {
        FGameDebugDraw::DrawBox(
            FGameDebugTimeController::ResolveWorld(WorldContextObject),
            Center,
            Extent,
            Rotation.Quaternion(),
            Color,
            Duration,
            Thickness
        );
    }
}

void UGameDebugBlueprintLibrary::DrawDebugCapsule(
    const UObject* WorldContextObject,
    FVector Center,
    float HalfHeight,
    float Radius,
    FRotator Rotation,
    FColor Color,
    float Duration,
    float Thickness
) {
    if constexpr (Game::Config::bEnableDebugDraw) {
        FGameDebugDraw::DrawCapsule(
            FGameDebugTimeController::ResolveWorld(WorldContextObject),
            Center,
            HalfHeight,
            Radius,
            Rotation.Quaternion(),
            Color,
            Duration,
            Thickness
        );
    }
}

void UGameDebugBlueprintLibrary::DrawDebugCylinder(
    const UObject* WorldContextObject,
    FVector Start,
    FVector End,
    float Radius,
    int32 Segments,
    FColor Color,
    float Duration,
    float Thickness
) {
    if constexpr (Game::Config::bEnableDebugDraw) {
        FGameDebugDraw::DrawCylinder(
            FGameDebugTimeController::ResolveWorld(WorldContextObject),
            Start,
            End,
            Radius,
            Segments,
            Color,
            Duration,
            Thickness
        );
    }
}

void UGameDebugBlueprintLibrary::DrawDebugCone(
    const UObject* WorldContextObject,
    FVector Origin,
    FVector Direction,
    float Length,
    float AngleWidthDeg,
    float AngleHeightDeg,
    int32 NumSides,
    FColor Color,
    float Duration,
    float Thickness
) {
    if constexpr (Game::Config::bEnableDebugDraw) {
        FGameDebugDraw::DrawCone(
            FGameDebugTimeController::ResolveWorld(WorldContextObject),
            Origin,
            Direction,
            Length,
            FMath::DegreesToRadians(AngleWidthDeg),
            FMath::DegreesToRadians(AngleHeightDeg),
            NumSides,
            Color,
            Duration,
            Thickness
        );
    }
}

void UGameDebugBlueprintLibrary::DrawDebugArrow(
    const UObject* WorldContextObject,
    FVector LineStart,
    FVector LineEnd,
    float ArrowSize,
    FColor Color,
    float Duration,
    float Thickness
) {
    if constexpr (Game::Config::bEnableDebugDraw) {
        FGameDebugDraw::DrawArrow(
            FGameDebugTimeController::ResolveWorld(WorldContextObject),
            LineStart,
            LineEnd,
            ArrowSize,
            Color,
            Duration,
            Thickness
        );
    }
}

void UGameDebugBlueprintLibrary::DrawDebugCoordinateSystem(
    const UObject* WorldContextObject,
    FVector AxisLoc,
    FRotator AxisRot,
    float Scale,
    float Duration,
    float Thickness
) {
    if constexpr (Game::Config::bEnableDebugDraw) {
        FGameDebugDraw::DrawCoordinateSystem(
            FGameDebugTimeController::ResolveWorld(WorldContextObject),
            AxisLoc,
            AxisRot,
            Scale,
            Duration,
            Thickness
        );
    }
}

void UGameDebugBlueprintLibrary::DrawDebugString(
    const UObject* WorldContextObject,
    FVector TextLocation,
    const FString& Text,
    FColor TextColor,
    float Duration,
    bool bDrawShadow,
    float FontScale
) {
    if constexpr (Game::Config::bEnableDebugDraw) {
        FGameDebugDraw::DrawString(
            FGameDebugTimeController::ResolveWorld(WorldContextObject),
            TextLocation,
            Text,
            nullptr,
            TextColor,
            Duration,
            bDrawShadow,
            FontScale
        );
    }
}

bool UGameDebugBlueprintLibrary::SetGlobalTimeDilation(const UObject* WorldContextObject, float TimeDilation) {
    return FGameDebugTimeController::SetTimeDilation(WorldContextObject, TimeDilation);
}

float UGameDebugBlueprintLibrary::GetGlobalTimeDilation(const UObject* WorldContextObject) {
    return FGameDebugTimeController::GetTimeDilation(WorldContextObject);
}

bool UGameDebugBlueprintLibrary::IsGamePaused(const UObject* WorldContextObject) {
    return FGameDebugTimeController::IsPaused(WorldContextObject);
}

bool UGameDebugBlueprintLibrary::PauseGameTime(const UObject* WorldContextObject) {
    return FGameDebugTimeController::Pause(WorldContextObject);
}

bool UGameDebugBlueprintLibrary::ResumeGameTime(const UObject* WorldContextObject) {
    return FGameDebugTimeController::Resume(WorldContextObject);
}

bool UGameDebugBlueprintLibrary::ResetGameTime(const UObject* WorldContextObject) {
    return FGameDebugTimeController::ResetTime(WorldContextObject);
}

bool UGameDebugBlueprintLibrary::StepGameFrames(const UObject* WorldContextObject, int32 Frames) {
    return FGameDebugTimeController::StepFrame(WorldContextObject, Frames);
}
