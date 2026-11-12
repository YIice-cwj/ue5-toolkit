/**
 * @file FGameQuantization.h
 * @brief 浮点、向量与旋转角纯数值量化压缩原语
 */

#pragma once

#include "CoreMinimal.h"
#include "Math/Float16.h"

/**
 * @brief 纯数值量化与紧凑数据打包算法集合
 */
class GAMECORE_API FGameQuantization {
public:
    /**
     * @brief 32 位单精度浮点转 IEEE 754 16 位半精度浮点编码
     * @param InValue 单精度浮点值
     * @return 16 位半精度编码整数
     */
    [[nodiscard]] static uint16 FloatToHalf(float InValue) noexcept {
        const FFloat16 Half(InValue);
        return Half.Encoded;
    }

    /**
     * @brief IEEE 754 16 位半精度浮点编码还原为 32 位单精度浮点
     * @param InHalf 16 位半精度编码整数
     * @return 32 位单精度浮点值
     */
    [[nodiscard]] static float HalfToFloat(uint16 InHalf) noexcept {
        FFloat16 Half;
        Half.Encoded = InHalf;
        return Half.GetFloat();
    }

    /**
     * @brief 将指定范围内的浮点数量化为 8 位无符号整数
     * @param InValue 输入浮点值
     * @param InMinValue 最小允许范围
     * @param InMaxValue 最大允许范围
     * @return [0, 255] 范围内的量化整数
     */
    [[nodiscard]] static uint8 QuantizeFloatToUint8(float InValue, float InMinValue, float InMaxValue) noexcept {
        return QuantizeFloatToUnsigned<uint8, 255>(InValue, InMinValue, InMaxValue);
    }

    /**
     * @brief 将 8 位量化整数还原为指定范围内的浮点数
     * @param InQuantized 量化整数
     * @param InMinValue 最小允许范围
     * @param InMaxValue 最大允许范围
     * @return 还原后的浮点值
     */
    [[nodiscard]] static float DequantizeUint8ToFloat(uint8 InQuantized, float InMinValue, float InMaxValue) noexcept {
        return DequantizeUnsignedToFloat<uint8, 255>(InQuantized, InMinValue, InMaxValue);
    }

    /**
     * @brief 将指定范围内的浮点数量化为 16 位无符号整数
     * @param InValue 输入浮点值
     * @param InMinValue 最小允许范围
     * @param InMaxValue 最大允许范围
     * @return [0, 65535] 范围内的量化整数
     */
    [[nodiscard]] static uint16 QuantizeFloatToUint16(float InValue, float InMinValue, float InMaxValue) noexcept {
        return QuantizeFloatToUnsigned<uint16, 65535>(InValue, InMinValue, InMaxValue);
    }

    /**
     * @brief 将 16 位量化整数还原为指定范围内的浮点数
     * @param InQuantized 量化整数
     * @param InMinValue 最小允许范围
     * @param InMaxValue 最大允许范围
     * @return 还原后的浮点值
     */
    [[nodiscard]] static float DequantizeUint16ToFloat(uint16 InQuantized, float InMinValue, float InMaxValue) noexcept {
        return DequantizeUnsignedToFloat<uint16, 65535>(InQuantized, InMinValue, InMaxValue);
    }

    /**
     * @brief 将三维向量按指定精度打包压缩为 64 位整数（X: 21位, Y: 21位, Z: 22位，先在双精度域钳制再转换，零溢出 UB）
     * @param InVector 输入三维空间坐标
     * @param InResolution 空间单位分辨率（默认 0.1，即 10cm 精度）
     * @return 压缩打包的 64 位整数
     */
    [[nodiscard]] static uint64 PackVectorToUint64(const FVector& InVector, double InResolution = 0.1) noexcept {
        const double SafeResolution = (FMath::IsFinite(InResolution) && InResolution > 0.0) ? InResolution : 0.1;
        constexpr double Min21 = -1048576.0;
        constexpr double Max21 = 1048575.0;
        constexpr double Min22 = -2097152.0;
        constexpr double Max22 = 2097151.0;

        const double RawX = FMath::IsFinite(InVector.X) ? FMath::RoundToDouble(InVector.X / SafeResolution) : 0.0;
        const double RawY = FMath::IsFinite(InVector.Y) ? FMath::RoundToDouble(InVector.Y / SafeResolution) : 0.0;
        const double RawZ = FMath::IsFinite(InVector.Z) ? FMath::RoundToDouble(InVector.Z / SafeResolution) : 0.0;

        const int32 QX = static_cast<int32>(FMath::Clamp(RawX, Min21, Max21));
        const int32 QY = static_cast<int32>(FMath::Clamp(RawY, Min21, Max21));
        const int32 QZ = static_cast<int32>(FMath::Clamp(RawZ, Min22, Max22));

        const uint64 MaskedX = static_cast<uint64>(static_cast<uint32>(QX) & 0x1FFFFF);
        const uint64 MaskedY = static_cast<uint64>(static_cast<uint32>(QY) & 0x1FFFFF);
        const uint64 MaskedZ = static_cast<uint64>(static_cast<uint32>(QZ) & 0x3FFFFF);

        return (MaskedX << 43) | (MaskedY << 22) | MaskedZ;
    }

    /**
     * @brief 将 64 位整数还原为三维空间坐标向量
     * @param InPacked 压缩打包的 64 位整数
     * @param InResolution 空间单位分辨率
     * @return 还原后的三维空间向量
     */
    [[nodiscard]] static FVector UnpackUint64ToVector(uint64 InPacked, double InResolution = 0.1) noexcept {
        const double SafeResolution = (FMath::IsFinite(InResolution) && InResolution > 0.0) ? InResolution : 0.1;

        const uint32 RawX = static_cast<uint32>((InPacked >> 43) & 0x1FFFFF);
        const uint32 RawY = static_cast<uint32>((InPacked >> 22) & 0x1FFFFF);
        const uint32 RawZ = static_cast<uint32>(InPacked & 0x3FFFFF);

        const int32 SignExtendedX = static_cast<int32>(RawX << 11) >> 11;
        const int32 SignExtendedY = static_cast<int32>(RawY << 11) >> 11;
        const int32 SignExtendedZ = static_cast<int32>(RawZ << 10) >> 10;

        return FVector(
            static_cast<double>(SignExtendedX) * SafeResolution,
            static_cast<double>(SignExtendedY) * SafeResolution,
            static_cast<double>(SignExtendedZ) * SafeResolution
        );
    }

    /**
     * @brief 将偏航角与俯仰角压缩打包为 16 位紧凑整数（各占 8 位）
     * @param InYawDegrees 偏航角（度，[-180, 180)）
     * @param InPitchDegrees 俯仰角（度，[-90, 90]）
     * @return 16 位打包整数
     */
    [[nodiscard]] static uint16 PackYawPitch(float InYawDegrees, float InPitchDegrees) noexcept {
        const float SafeYaw = FMath::IsFinite(InYawDegrees) ? InYawDegrees : 0.0f;
        const float SafePitch = FMath::IsFinite(InPitchDegrees) ? InPitchDegrees : 0.0f;
        const float NormalizedYaw = FRotator::NormalizeAxis(SafeYaw);
        const uint8 PackedYaw = static_cast<uint8>(FMath::RoundToInt(((NormalizedYaw + 180.0f) / 360.0f) * 255.0f));

        const float ClampedPitch = FMath::Clamp(SafePitch, -90.0f, 90.0f);
        const uint8 PackedPitch = static_cast<uint8>(FMath::RoundToInt(((ClampedPitch + 90.0f) / 180.0f) * 255.0f));

        return static_cast<uint16>((static_cast<uint16>(PackedYaw) << 8) | static_cast<uint16>(PackedPitch));
    }

    /**
     * @brief 将 16 位打包整数解压为偏航角与俯仰角
     * @param InPacked 16 位打包整数
     * @param OutYawDegrees 输出偏航角（度）
     * @param OutPitchDegrees 输出俯仰角（度）
     */
    static void UnpackYawPitch(uint16 InPacked, float& OutYawDegrees, float& OutPitchDegrees) noexcept {
        const uint8 PackedYaw = static_cast<uint8>((InPacked >> 8) & 0xFF);
        const uint8 PackedPitch = static_cast<uint8>(InPacked & 0xFF);

        OutYawDegrees = (static_cast<float>(PackedYaw) / 255.0f * 360.0f) - 180.0f;
        OutPitchDegrees = (static_cast<float>(PackedPitch) / 255.0f * 180.0f) - 90.0f;
    }

private:
    /**
     * @brief 将指定闭区间内的浮点数量化映射到无符号整数值域 [0, MaxScale]
     * @tparam TUInt 目标无符号整数类型
     * @tparam MaxScale 最大量化刻度值
     * @param InValue 输入浮点值
     * @param InMinValue 区间下界
     * @param InMaxValue 区间上界
     * @return 量化后的无符号整数
     */
    template <typename TUInt, uint32 MaxScale>
    [[nodiscard]] static TUInt QuantizeFloatToUnsigned(float InValue, float InMinValue, float InMaxValue) noexcept {
        if (!FMath::IsFinite(InValue) || !FMath::IsFinite(InMinValue) || !FMath::IsFinite(InMaxValue) || InMaxValue <= InMinValue) {
            return 0;
        }
        const float Clamped = FMath::Clamp(InValue, InMinValue, InMaxValue);
        const float Normalized = (Clamped - InMinValue) / (InMaxValue - InMinValue);
        return static_cast<TUInt>(FMath::RoundToInt(Normalized * static_cast<float>(MaxScale)));
    }

    /**
     * @brief 将 [0, MaxScale] 范围内的无符号量化整数线性还原为闭区间浮点数
     * @tparam TUInt 源无符号整数类型
     * @tparam MaxScale 最大量化刻度值
     * @param InQuantized 量化整数
     * @param InMinValue 区间下界
     * @param InMaxValue 区间上界
     * @return 还原后的浮点值
     */
    template <typename TUInt, uint32 MaxScale>
    [[nodiscard]] static float DequantizeUnsignedToFloat(TUInt InQuantized, float InMinValue, float InMaxValue) noexcept {
        if (!FMath::IsFinite(InMinValue) || !FMath::IsFinite(InMaxValue) || InMaxValue <= InMinValue) {
            return FMath::IsFinite(InMinValue) ? InMinValue : 0.0f;
        }
        const float Normalized = static_cast<float>(InQuantized) / static_cast<float>(MaxScale);
        return InMinValue + Normalized * (InMaxValue - InMinValue);
    }
};
