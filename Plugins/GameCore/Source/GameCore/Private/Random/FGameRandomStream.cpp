/**
 * @file FGameRandomStream.cpp
 * @brief FGameRandomStream 确定性隔离伪随机流实现
 */

#include "Random/FGameRandomStream.h"

/**
 * @brief 默认构造函数（使用固定默认种子初始化）
 */
FGameRandomStream::FGameRandomStream() noexcept {
    Initialize(DEFAULT_SEED);
}

/**
 * @brief 使用指定 64 位种子构造随机流
 * @param InSeed 初始种子
 */
FGameRandomStream::FGameRandomStream(uint64 InSeed) noexcept {
    Initialize(InSeed);
}

/**
 * @brief 使用新种子重新混合并初始化内部状态
 * @param InSeed 64 位初始种子
 */
void FGameRandomStream::Initialize(uint64 InSeed) noexcept {
    CurrentState = 0ULL;
    AdvanceState();
    CurrentState += MixSeedSplitMix64(InSeed);
    AdvanceState();
}

/**
 * @brief 推进状态并生成下一个 32 位均匀分布无符号随机整数
 * @return 32 位无符号随机整数
 */
uint32 FGameRandomStream::NextUInt32() noexcept {
    const uint64 OldState = CurrentState;
    AdvanceState();
    const uint32 XorShifted = static_cast<uint32>(((OldState >> 18u) ^ OldState) >> 27u);
    const uint32 Rot = static_cast<uint32>(OldState >> 59u);
    return (XorShifted >> Rot) | (XorShifted << ((0u - Rot) & 31u));
}

/**
 * @brief 生成 [0, Bound) 半开区间内无取模偏差的 32 位无符号随机整数
 * @param Bound 上界（为 0 或 1 时返回 0）
 * @return 无偏随机整数
 */
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

/**
 * @brief 生成闭区间 [Min, Max] 内的无偏随机有符号整数
 * @param Min 区间下界
 * @param Max 区间上界
 * @return 闭区间随机整数
 */
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

/**
 * @brief 生成 [0.0f, 1.0f) 半开区间内的单精度均匀随机浮点数
 * @return [0.0f, 1.0f) 随机浮点数
 */
float FGameRandomStream::FRand() noexcept {
    return static_cast<float>(NextUInt32() >> 8u) * (1.0f / 16777216.0f);
}

/**
 * @brief 生成 [Min, Max] 区间内的均匀随机浮点数
 * @param Min 区间下界
 * @param Max 区间上界
 * @return 随机浮点数
 */
float FGameRandomStream::FRandRange(float Min, float Max) noexcept {
    if (Min > Max) {
        Swap(Min, Max);
    }
    return Min + (Max - Min) * FRand();
}

/**
 * @brief 按给定概率执行一次伯努利判定
 * @param Probability 成功概率
 * @return 命中返回 true
 */
bool FGameRandomStream::RollChance(float Probability) noexcept {
    if (Probability <= 0.0f) {
        return false;
    }
    if (Probability >= 1.0f) {
        return true;
    }
    return FRand() < Probability;
}

/**
 * @brief 在三维单位球面上执行均匀立体角随机方向采样
 * @return 单位长度随机三维方向向量
 */
FVector FGameRandomStream::RandomUnitVector() noexcept {
    const float Z = FRandRange(-1.0f, 1.0f);
    const float Theta = 2.0f * UE_PI * FRand();
    const float PlanarRadius = FMath::Sqrt(FMath::Max(0.0f, 1.0f - Z * Z));
    float SinTheta = 0.0f;
    float CosTheta = 1.0f;
    FMath::SinCos(&SinTheta, &CosTheta, Theta);
    return FVector(PlanarRadius * CosTheta, PlanarRadius * SinTheta, Z);
}

/**
 * @brief 在指定轴向与半张角的圆锥立体角内执行均匀随机方向采样
 * @param ConeDir 圆锥中心轴向量
 * @param ConeHalfAngleRad 圆锥半张角弧度
 * @return 圆锥内单位长度随机方向向量
 */
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

/**
 * @brief 二进制归档序列化流状态
 * @param Ar 归档器引用
 */
void FGameRandomStream::SerializeState(FArchive& Ar) {
    Ar << CurrentState;
}

/**
 * @brief 执行单步 PCG32 线性同余状态推进
 */
void FGameRandomStream::AdvanceState() noexcept {
    CurrentState = CurrentState * PCG_MULTIPLIER + PCG_INCREMENT;
}

/**
 * @brief 使用 SplitMix64 双重雪崩散列将任意 64 位种子混合为高熵非零初始状态
 * @param Seed 原始 64 位种子
 * @return 混合后的 64 位状态值
 */
uint64 FGameRandomStream::MixSeedSplitMix64(uint64 Seed) noexcept {
    uint64 Value = Seed + 0x9E3779B97F4A7C15ULL;
    Value = (Value ^ (Value >> 30u)) * 0xBF58476D1CE4E5B9ULL;
    Value = (Value ^ (Value >> 27u)) * 0x94D049BB133111EBULL;
    return Value ^ (Value >> 31u);
}
