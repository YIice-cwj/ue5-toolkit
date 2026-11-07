/**
 * @file FGameMath.cpp
 * @brief FGameMath 帧率无关阻尼平滑与空间几何工具实现
 */

#include "Math/FGameMath.h"

FGameMath::FSpringFloatState FGameMath::CriticalSpringDampFloat(
    FSpringFloatState CurrentState,
    float TargetValue,
    float SmoothTime,
    float DeltaSeconds
) noexcept {
    if (DeltaSeconds <= 0.0f) {
        return CurrentState;
    }

    const float ClampedSmoothTime = FMath::Max(MIN_SMOOTH_TIME, SmoothTime);
    const float Omega = 2.0f / ClampedSmoothTime;
    const float X = Omega * DeltaSeconds;
    const float ExpFactor = 1.0f / (1.0f + X + 0.48f * X * X + 0.235f * X * X * X);

    const float DeltaValue = CurrentState.Value - TargetValue;
    const float Temp = (CurrentState.Velocity + Omega * DeltaValue) * DeltaSeconds;
    const float NextVelocity = (CurrentState.Velocity - Omega * Temp) * ExpFactor;
    const float NextValue = TargetValue + (DeltaValue + Temp) * ExpFactor;

    return FSpringFloatState{NextValue, NextVelocity};
}

FGameMath::FSpringVectorState FGameMath::CriticalSpringDampVector(
    const FSpringVectorState& CurrentState,
    const FVector& TargetValue,
    float SmoothTime,
    float DeltaSeconds
) noexcept {
    if (DeltaSeconds <= 0.0f) {
        return CurrentState;
    }

    const double ClampedSmoothTime = static_cast<double>(FMath::Max(MIN_SMOOTH_TIME, SmoothTime));
    const double Dt = static_cast<double>(DeltaSeconds);
    const double Omega = 2.0 / ClampedSmoothTime;
    const double X = Omega * Dt;
    const double ExpFactor = 1.0 / (1.0 + X + 0.48 * X * X + 0.235 * X * X * X);

    const FVector DeltaVec = CurrentState.Value - TargetValue;
    const FVector TempVec = (CurrentState.Velocity + DeltaVec * Omega) * Dt;
    const FVector NextVelocity = (CurrentState.Velocity - TempVec * Omega) * ExpFactor;
    const FVector NextValue = TargetValue + (DeltaVec + TempVec) * ExpFactor;

    return FSpringVectorState{NextValue, NextVelocity};
}

float FGameMath::SignedYawDeltaDegrees(const FVector& ForwardDir, const FVector& ToTargetDir) noexcept {
    const FVector2D FlatForward(ForwardDir.X, ForwardDir.Y);
    const FVector2D FlatTarget(ToTargetDir.X, ToTargetDir.Y);

    const double ForwardLenSq = FlatForward.SizeSquared();
    const double TargetLenSq = FlatTarget.SizeSquared();
    if (ForwardLenSq <= UE_DOUBLE_SMALL_NUMBER || TargetLenSq <= UE_DOUBLE_SMALL_NUMBER) {
        return 0.0f;
    }

    const double CrossZ = FlatForward.X * FlatTarget.Y - FlatForward.Y * FlatTarget.X;
    const double DotXY = FlatForward.X * FlatTarget.X + FlatForward.Y * FlatTarget.Y;
    const double Radians = FMath::Atan2(CrossZ, DotXY);
    return static_cast<float>(FMath::RadiansToDegrees(Radians));
}

bool FGameMath::IsInVisionCone(
    const FVector& OffsetToTarget,
    const FVector& ConeForward,
    float MaxDistance,
    float HalfAngleDegrees
) noexcept {
    if (MaxDistance <= 0.0f || HalfAngleDegrees < 0.0f) {
        return false;
    }

    const FVector NormalizedAxis = ConeForward.GetSafeNormal();
    if (NormalizedAxis.IsZero()) {
        return false;
    }

    const double DistSq = OffsetToTarget.SizeSquared();
    const double MaxDistSq = static_cast<double>(MaxDistance) * static_cast<double>(MaxDistance);
    if (DistSq > MaxDistSq) {
        return false;
    }
    if (DistSq <= UE_DOUBLE_KINDA_SMALL_NUMBER) {
        return true;
    }

    const float ClampedHalfAngle = FMath::Clamp(HalfAngleDegrees, 0.0f, 180.0f);
    if (ClampedHalfAngle >= 180.0f) {
        return true;
    }

    const FVector DirToTarget = OffsetToTarget / FMath::Sqrt(DistSq);
    const double CosAngle = FVector::DotProduct(NormalizedAxis, DirToTarget);
    const double CosThreshold = FMath::Cos(FMath::DegreesToRadians(static_cast<double>(ClampedHalfAngle)));
    return CosAngle >= (CosThreshold - 1e-6);
}
