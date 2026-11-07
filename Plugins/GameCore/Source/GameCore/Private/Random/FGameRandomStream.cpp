/**
 * @file FGameRandomStream.cpp
 * @brief FGameRandomStream 确定性隔离伪随机流实现
 */

#include "Random/FGameRandomStream.h"

FGameRandomStream::FGameRandomStream() noexcept {
    Initialize(DEFAULT_SEED);
}

FGameRandomStream::FGameRandomStream(uint64 InSeed) noexcept {
    Initialize(InSeed);
}

void FGameRandomStream::Initialize(uint64 InSeed) noexcept {
    CurrentState = 0ULL;
    AdvanceState();
    CurrentState += MixSeedSplitMix64(InSeed);
    AdvanceState();
}

uint32 FGameRandomStream::NextUInt32() noexcept {
    const uint64 OldState = CurrentState;
    AdvanceState();
    const uint32 XorShifted = static_cast<uint32>(((OldState >> 18u) ^ OldState) >> 27u);
    const uint32 Rot = static_cast<uint32>(OldState >> 59u);
    return (XorShifted >> Rot) | (XorShifted << ((0u - Rot) & 31u));
}

uint32 FGameRandomStream::NextBoundedUInt32(uint32 Bound) noexcept {
    if (Bound <= 1u) {
        return 0u;
    }
    const uint32 Threshold = (0u - Bound) % Bound;
    for (;;) {
        const uint32 Candidate = NextUInt32();
        if (Candidate >= Threshold) {
            return Candidate % Bound;
        }
    }
}

int32 FGameRandomStream::RandRange(int32 Min, int32 Max) noexcept {
    if (Min > Max) {
        Swap(Min, Max);
    }
    const uint64 Span = static_cast<uint64>(static_cast<int64>(Max) - static_cast<int64>(Min));
    if (Span == 0ULL) {
        return Min;
    }
    if (Span == static_cast<uint64>(MAX_uint32)) {
        const uint32 Raw = NextUInt32();
        return static_cast<int32>(static_cast<int64>(Min) + static_cast<int64>(Raw));
    }
    const uint32 Offset = NextBoundedUInt32(static_cast<uint32>(Span + 1ULL));
    return static_cast<int32>(static_cast<int64>(Min) + static_cast<int64>(Offset));
}

float FGameRandomStream::FRand() noexcept {
    return static_cast<float>(NextUInt32() >> 8u) * (1.0f / 16777216.0f);
}

float FGameRandomStream::FRandRange(float Min, float Max) noexcept {
    if (Min > Max) {
        Swap(Min, Max);
    }
    return Min + (Max - Min) * FRand();
}

bool FGameRandomStream::RollChance(float Probability) noexcept {
    if (Probability <= 0.0f) {
        return false;
    }
    if (Probability >= 1.0f) {
        return true;
    }
    return FRand() < Probability;
}

FVector FGameRandomStream::RandomUnitVector() noexcept {
    const float Z = FRandRange(-1.0f, 1.0f);
    const float Theta = 2.0f * UE_PI * FRand();
    const float PlanarRadius = FMath::Sqrt(FMath::Max(0.0f, 1.0f - Z * Z));
    float SinTheta = 0.0f;
    float CosTheta = 1.0f;
    FMath::SinCos(&SinTheta, &CosTheta, Theta);
    return FVector(PlanarRadius * CosTheta, PlanarRadius * SinTheta, Z);
}

FVector FGameRandomStream::RandomVectorInConeRad(const FVector& ConeDir, float ConeHalfAngleRad) noexcept {
    const FVector AxisZ = ConeDir.GetSafeNormal();
    if (AxisZ.IsZero()) {
        return FVector::ZeroVector;
    }

    const float ClampedAngle = FMath::Clamp(ConeHalfAngleRad, 0.0f, UE_PI);
    if (ClampedAngle <= KINDA_SMALL_NUMBER) {
        return AxisZ;
    }

    const float CosMin = FMath::Cos(ClampedAngle);
    const float CosPhi = CosMin + (1.0f - CosMin) * FRand();
    const float SinPhi = FMath::Sqrt(FMath::Max(0.0f, 1.0f - CosPhi * CosPhi));
    const float Theta = 2.0f * UE_PI * FRand();

    float SinTheta = 0.0f;
    float CosTheta = 1.0f;
    FMath::SinCos(&SinTheta, &CosTheta, Theta);

    FVector AxisX = FVector::ZeroVector;
    FVector AxisY = FVector::ZeroVector;
    AxisZ.FindBestAxisVectors(AxisX, AxisY);

    return (AxisX * (SinPhi * CosTheta) + AxisY * (SinPhi * SinTheta) + AxisZ * CosPhi).GetSafeNormal();
}

void FGameRandomStream::SerializeState(FArchive& Ar) {
    Ar << CurrentState;
}

void FGameRandomStream::AdvanceState() noexcept {
    CurrentState = CurrentState * PCG_MULTIPLIER + PCG_INCREMENT;
}

uint64 FGameRandomStream::MixSeedSplitMix64(uint64 Seed) noexcept {
    uint64 Value = Seed + 0x9E3779B97F4A7C15ULL;
    Value = (Value ^ (Value >> 30u)) * 0xBF58476D1CE4E5B9ULL;
    Value = (Value ^ (Value >> 27u)) * 0x94D049BB133111EBULL;
    return Value ^ (Value >> 31u);
}
