// Copyright Epic Games, Inc. All Rights Reserved.

#include "Determinism/FStateChecksumBuilder.h"

#include "Misc/StringBuilder.h"

namespace {

static constexpr uint64 GInitialHashSeed = 0x9E3779B97F4A7C15ULL;
static constexpr uint64 GNonFiniteFloatSentinel = 0x7FF8000000000001ULL;
static constexpr double GMaxSafeInt64Double = 9223372036854774784.0;

/**
 * @brief 执行 MurmurHash3 64 位终态雪崩位混合
 * @param Value 输入 64 位整数
 * @return 雪崩混合后的 64 位散列值
 */
[[nodiscard]] constexpr uint64 Mix64(uint64 Value) noexcept {
    Value ^= Value >> 33;
    Value *= 0xff51afd7ed558ccdULL;
    Value ^= Value >> 33;
    Value *= 0xc4ceb9fe1a85ec53ULL;
    Value ^= Value >> 33;
    return Value;
}

/**
 * @brief 将有限浮点数按指定精度安全量化为跨架构位一致的 64 位整数（消除除法溢出与 RoundToInt64 跨平台 UB）
 * @param Value 有限输入浮点值
 * @param SafePrecision 已校验的有限正量化精度
 * @return 量化后的 64 位无符号编码（缩放溢出非有限时返回 GNonFiniteFloatSentinel）
 */
[[nodiscard]] inline uint64 QuantizeDoubleSafe(double Value, double SafePrecision) noexcept {
    const double Scaled = Value / SafePrecision;
    if (!FMath::IsFinite(Scaled)) {
        return GNonFiniteFloatSentinel;
    }
    const double Clamped = FMath::Clamp(Scaled, -GMaxSafeInt64Double, GMaxSafeInt64Double);
    return static_cast<uint64>(FMath::RoundToInt64(Clamped));
}

}  // namespace

/**
 * @brief 构造函数：初始化哈希种子
 */
FStateChecksumBuilder::FStateChecksumBuilder() noexcept
    : HashState(GInitialHashSeed) {
}

/**
 * @brief 注入布尔值
 * @param bValue 输入布尔值
 */
void FStateChecksumBuilder::AddBool(bool bValue) noexcept {
    AddUInt32(bValue ? 1U : 0U);
}

/**
 * @brief 注入 32 位有符号整数
 * @param Value 输入数值
 */
void FStateChecksumBuilder::AddInt32(int32 Value) noexcept {
    AddUInt32(static_cast<uint32>(Value));
}

/**
 * @brief 注入 64 位有符号整数
 * @param Value 输入数值
 */
void FStateChecksumBuilder::AddInt64(int64 Value) noexcept {
    AddUInt64(static_cast<uint64>(Value));
}

/**
 * @brief 注入 32 位无符号整数
 * @param Value 输入数值
 */
void FStateChecksumBuilder::AddUInt32(uint32 Value) noexcept {
    HashState = Mix64(HashState ^ (static_cast<uint64>(Value) * 0x517cc1b727220a95ULL));
}

/**
 * @brief 注入 64 位无符号整数
 * @param Value 输入数值
 */
void FStateChecksumBuilder::AddUInt64(uint64 Value) noexcept {
    HashState = Mix64(HashState ^ (Value * 0x9E3779B97F4A7C15ULL));
}

/**
 * @brief 注入字符串视图（先混入长度定界帧再注入字符字节流，杜绝相邻字符串拼接碰撞与空串退化）
 * @param Str 输入字符串视图
 */
void FStateChecksumBuilder::AddString(FStringView Str) noexcept {
    AddInt32(Str.Len());
    if (!Str.IsEmpty()) {
        AddBytes(Str.GetData(), static_cast<size_t>(Str.Len()) * sizeof(TCHAR));
    }
}

/**
 * @brief 注入 FName（使用栈上 TStringBuilder 零堆内存分配格式化并注入）
 * @param Name 输入 FName
 */
void FStateChecksumBuilder::AddName(const FName& Name) noexcept {
    TStringBuilder<NAME_SIZE> NameBuilder;
    Name.AppendString(NameBuilder);
    AddString(NameBuilder.ToView());
}

/**
 * @brief 注入按指定精度量化截断的单精度浮点数
 * @param Value 输入浮点数
 * @param Precision 量化精度
 */
void FStateChecksumBuilder::AddFloatQuantized(float Value, float Precision) noexcept {
    if (!FMath::IsFinite(Value)) {
        AddUInt64(GNonFiniteFloatSentinel);
        return;
    }
    const double SafePrecision = (FMath::IsFinite(Precision) && Precision > 1e-7f)
        ? static_cast<double>(Precision)
        : 0.001;
    AddUInt64(QuantizeDoubleSafe(static_cast<double>(Value), SafePrecision));
}

/**
 * @brief 注入按指定精度量化截断的双精度浮点数
 * @param Value 输入双精度浮点数
 * @param Precision 量化精度
 */
void FStateChecksumBuilder::AddDoubleQuantized(double Value, double Precision) noexcept {
    if (!FMath::IsFinite(Value)) {
        AddUInt64(GNonFiniteFloatSentinel);
        return;
    }
    const double SafePrecision = (FMath::IsFinite(Precision) && Precision > 1e-12) ? Precision : 0.001;
    AddUInt64(QuantizeDoubleSafe(Value, SafePrecision));
}

/**
 * @brief 注入按指定精度量化截断的三维空间向量
 * @param Vec 输入三维向量
 * @param Precision 量化精度
 */
void FStateChecksumBuilder::AddVectorQuantized(const FVector& Vec, double Precision) noexcept {
    AddDoubleQuantized(Vec.X, Precision);
    AddDoubleQuantized(Vec.Y, Precision);
    AddDoubleQuantized(Vec.Z, Precision);
}

/**
 * @brief 注入原生二进制连续字节流（按 8 字节小端定序批量混入 + 尾字节长度帧折叠，跨架构位一致且零对齐 UB）
 * @param Data 字节流首地址
 * @param Size 字节数
 */
void FStateChecksumBuilder::AddBytes(const void* Data, size_t Size) noexcept {
    if (Data == nullptr || Size == 0) {
        return;
    }

    const uint8* BytePtr = static_cast<const uint8*>(Data);
    size_t Offset = 0;

    while (Offset + sizeof(uint64) <= Size) {
        uint64 Word = 0ULL;
        FMemory::Memcpy(&Word, BytePtr + Offset, sizeof(uint64));
#if !PLATFORM_LITTLE_ENDIAN
        Word = BYTESWAP_ORDER64(Word);
#endif
        HashState = Mix64(HashState ^ (Word * 0x9E3779B97F4A7C15ULL));
        Offset += sizeof(uint64);
    }

    const size_t Remaining = Size - Offset;
    if (Remaining > 0) {
        uint64 TailWord = 0ULL;
        for (size_t Index = 0; Index < Remaining; ++Index) {
            TailWord |= static_cast<uint64>(BytePtr[Offset + Index]) << (Index * 8U);
        }
        TailWord ^= (static_cast<uint64>(Remaining) << 56U);
        HashState = Mix64(HashState ^ (TailWord * 0x100000001b3ULL));
    }
}

/**
 * @brief 计算并输出最终的 64 位确定性状态哈希码
 * @return 最终校验码
 */
uint64 FStateChecksumBuilder::Finalize() const noexcept {
    return Mix64(HashState);
}

/**
 * @brief 重置哈希状态机以供复用
 */
void FStateChecksumBuilder::Reset() noexcept {
    HashState = GInitialHashSeed;
}
