/**
 * @file FGameDebugTrailRecorder.cpp
 * @brief 现代化 3D 空间运动轨迹记录器实现
 */

#include "Visualizer/FGameDebugTrailRecorder.h"

#include "Config/GameCompileSwitches.h"
#include "HAL/PlatformTime.h"
#include "Visualizer/FGameDebugDraw.h"

FGameDebugTrailRecorder::FGameDebugTrailRecorder(
    int32 InMaxSamples,
    float InLifetimeSeconds,
    float InMinDistance
)
    : MaxSamples(FMath::Max(2, InMaxSamples))
    , HeadIndex(0)
    , SampleCount(0)
    , LifetimeSeconds((FMath::IsFinite(InLifetimeSeconds) && InLifetimeSeconds > 0.1f) ? InLifetimeSeconds : 0.1f)
    , MinDistanceSquared(FMath::Square(static_cast<double>((FMath::IsFinite(InMinDistance) && InMinDistance > 0.0f) ? InMinDistance : 0.0f))) {
    if constexpr (Game::Config::bEnableDebugDraw) {
        RingBuffer.SetNum(MaxSamples);
    }
}

bool FGameDebugTrailRecorder::AddSample(const FVector& NewLocation, double CurrentTimeSeconds) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return false;
    }

    if (NewLocation.ContainsNaN() || !FMath::IsFinite(CurrentTimeSeconds)) {
        return false;
    }

    double Now = (CurrentTimeSeconds >= 0.0) ? CurrentTimeSeconds : FPlatformTime::Seconds();
    if (SampleCount > 0) {
        const double LastTimestamp = RingBuffer[ToPhysicalIndex(SampleCount - 1)].Timestamp;
        if (Now < LastTimestamp) {
            Now = LastTimestamp;
        }
    }

    PruneExpired(Now);

    if (SampleCount > 0 && MinDistanceSquared > 0.0) {
        const FVector& LastLoc = RingBuffer[ToPhysicalIndex(SampleCount - 1)].Location;
        if (FVector::DistSquared(LastLoc, NewLocation) < MinDistanceSquared) {
            return false;
        }
    }

    if (SampleCount < MaxSamples) {
        const int32 WriteIndex = ToPhysicalIndex(SampleCount);
        RingBuffer[WriteIndex] = FTrailSample{NewLocation, Now};
        ++SampleCount;
    } else {
        RingBuffer[HeadIndex] = FTrailSample{NewLocation, Now};
        ++HeadIndex;
        if (HeadIndex >= MaxSamples) {
            HeadIndex = 0;
        }
    }
    return true;
}

void FGameDebugTrailRecorder::DrawTrail(
    const UWorld* World,
    const FColor& HeadColor,
    const FColor& TailColor,
    float Thickness,
    float Duration,
    double CurrentTimeSeconds
) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    }

    if (!FMath::IsFinite(CurrentTimeSeconds)) {
        return;
    }

    const double Now = (CurrentTimeSeconds >= 0.0) ? CurrentTimeSeconds : FPlatformTime::Seconds();
    PruneExpired(Now);

    const bool bValidStyle = FMath::IsFinite(Thickness) && Thickness >= 0.0f && FMath::IsFinite(Duration) && Duration >= 0.0f;
    if (World == nullptr || SampleCount < 2 || !bValidStyle) {
        return;
    }

    const FLinearColor TailLinear(TailColor);
    const FLinearColor HeadLinear(HeadColor);
    const double InvLifetime = 1.0 / static_cast<double>(LifetimeSeconds);

    for (int32 Index = 0; Index < SampleCount - 1; ++Index) {
        const FTrailSample& StartSample = RingBuffer[ToPhysicalIndex(Index)];
        const FTrailSample& EndSample = RingBuffer[ToPhysicalIndex(Index + 1)];
        const double AgeSeconds = Now - EndSample.Timestamp;
        const float TimeAlpha = FMath::Clamp(1.0f - static_cast<float>(AgeSeconds * InvLifetime), 0.0f, 1.0f);
        const FColor SegmentColor = FMath::Lerp(TailLinear, HeadLinear, TimeAlpha).ToFColor(true);

        FGameDebugDraw::DrawLine(
            World,
            StartSample.Location,
            EndSample.Location,
            SegmentColor,
            Duration,
            Thickness
        );
    }
}

void FGameDebugTrailRecorder::Clear() noexcept {
    HeadIndex = 0;
    SampleCount = 0;
}

int32 FGameDebugTrailRecorder::GetSampleCount() const noexcept {
    return SampleCount;
}

const FGameDebugTrailRecorder::FTrailSample& FGameDebugTrailRecorder::GetSample(int32 Index) const {
    check(Index >= 0 && Index < SampleCount);
    return RingBuffer[ToPhysicalIndex(Index)];
}

void FGameDebugTrailRecorder::PruneExpired(double CurrentTimeSeconds) noexcept {
    const double ExpireCutoff = CurrentTimeSeconds - static_cast<double>(LifetimeSeconds);
    while (SampleCount > 0 && RingBuffer[HeadIndex].Timestamp < ExpireCutoff) {
        ++HeadIndex;
        if (HeadIndex >= MaxSamples) {
            HeadIndex = 0;
        }
        --SampleCount;
    }
    if (SampleCount == 0) {
        HeadIndex = 0;
    }
}

int32 FGameDebugTrailRecorder::ToPhysicalIndex(int32 LogicalIndex) const noexcept {
    int32 PhysicalIndex = HeadIndex + LogicalIndex;
    if (PhysicalIndex >= MaxSamples) {
        PhysicalIndex -= MaxSamples;
    }
    return PhysicalIndex;
}
