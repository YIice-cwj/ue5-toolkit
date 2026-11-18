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
    , LifetimeSeconds(FMath::Max(0.1f, InLifetimeSeconds))
    , MinDistanceSquared(FMath::Square(FMath::Max(0.0f, InMinDistance))) {
    Samples.Reserve(MaxSamples);
}

bool FGameDebugTrailRecorder::AddSample(const FVector& NewLocation, double CurrentTimeSeconds) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return false;
    } else {
        const double Now = (CurrentTimeSeconds > 0.0) ? CurrentTimeSeconds : FPlatformTime::Seconds();

        if (!Samples.IsEmpty()) {
            const FVector& LastLoc = Samples.Last().Location;
            if (FVector::DistSquared(LastLoc, NewLocation) < MinDistanceSquared) {
                return false;
            }
        }

        const int32 ExcessCount = Samples.Num() - MaxSamples + 1;
        if (ExcessCount > 0) {
            Samples.RemoveAt(0, ExcessCount, EAllowShrinking::No);
        }

        Samples.Add(FTrailSample{NewLocation, Now});
        return true;
    }
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
    } else {
        if (World == nullptr || Samples.Num() < 2) {
            return;
        }

        const double Now = (CurrentTimeSeconds > 0.0) ? CurrentTimeSeconds : FPlatformTime::Seconds();

        const double ExpireCutoff = Now - static_cast<double>(LifetimeSeconds);
        int32 ValidStartIndex = 0;
        while (ValidStartIndex < Samples.Num() && Samples[ValidStartIndex].Timestamp < ExpireCutoff) {
            ++ValidStartIndex;
        }

        if (ValidStartIndex > 0) {
            Samples.RemoveAt(0, ValidStartIndex, EAllowShrinking::No);
        }

        const int32 NumSegments = Samples.Num() - 1;
        if (NumSegments <= 0) {
            return;
        }

        for (int32 Index = 0; Index < NumSegments; ++Index) {
            const float Alpha = static_cast<float>(Index) / static_cast<float>(NumSegments);
            const FColor SegmentColor = FMath::Lerp(FLinearColor(TailColor), FLinearColor(HeadColor), Alpha).ToFColor(true);

            FGameDebugDraw::DrawLine(
                World,
                Samples[Index].Location,
                Samples[Index + 1].Location,
                SegmentColor,
                Duration,
                Thickness
            );
        }
    }
}

void FGameDebugTrailRecorder::Clear() {
    Samples.Reset();
}

int32 FGameDebugTrailRecorder::GetSampleCount() const noexcept {
    return Samples.Num();
}

const FGameDebugTrailRecorder::FTrailSample& FGameDebugTrailRecorder::GetSample(int32 Index) const {
    check(Samples.IsValidIndex(Index));
    return Samples[Index];
}
