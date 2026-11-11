/**
 * @file FGameDistId.h
 * @brief 64 位分布式单调递增雪花唯一 ID 值类型原语（雪崩散列 · 字符串双向序列化 · 绝对零堆分配）
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/StringView.h"

namespace Game::Hash {

/**
 * @brief 64 位整数极致雪崩散列算法（MurmurHash3 64-bit Finalizer，单周期无分支位运算）
 * @param Value 待散列 64 位数值
 * @return 散列均匀的 32 位哈希码
 */
[[nodiscard]] inline constexpr uint32 Hash64To32(uint64 Value) noexcept {
    Value ^= Value >> 33;
    Value *= 0xff51afd7ed558ccdULL;
    Value ^= Value >> 33;
    Value *= 0xc4ceb9fe1a85ec53ULL;
    Value ^= Value >> 33;
    return static_cast<uint32>(Value ^ (Value >> 32));
}

} // namespace Game::Hash

namespace Game::Private {

/**
 * @brief 零堆分配高性能解析十进制或 0x 前缀十六进制无符号 64 位整数
 * @param InView 输入字符串切片
 * @param OutValue 传出解析结果
 * @return 解析成功且无溢出返回 true，存在非法字符或溢出返回 false
 */
inline bool ParseUInt64FromView(FStringView InView, uint64& OutValue) noexcept {
    InView = InView.TrimStartAndEnd();
    if (InView.IsEmpty()) {
        return false;
    }
    bool bHex = false;
    if (InView.StartsWith(TEXT("0x"), ESearchCase::IgnoreCase)) {
        bHex = true;
        InView.RemovePrefix(2);
        if (InView.IsEmpty()) {
            return false;
        }
    }
    uint64 Result = 0;
    if (bHex) {
        for (const TCHAR Ch : InView) {
            uint64 Digit = 0;
            if (Ch >= TEXT('0') && Ch <= TEXT('9')) {
                Digit = Ch - TEXT('0');
            } else if (Ch >= TEXT('a') && Ch <= TEXT('f')) {
                Digit = 10 + (Ch - TEXT('a'));
            } else if (Ch >= TEXT('A') && Ch <= TEXT('F')) {
                Digit = 10 + (Ch - TEXT('A'));
            } else {
                return false;
            }
            if (Result > (0xFFFFFFFFFFFFFFFFULL >> 4)) {
                return false;
            }
            Result = (Result << 4) | Digit;
        }
    } else {
        for (const TCHAR Ch : InView) {
            if (Ch < TEXT('0') || Ch > TEXT('9')) {
                return false;
            }
            const uint64 Digit = Ch - TEXT('0');
            if (Result > (0xFFFFFFFFFFFFFFFFULL - Digit) / 10ULL) {
                return false;
            }
            Result = Result * 10ULL + Digit;
        }
    }
    OutValue = Result;
    return true;
}

} // namespace Game::Private

/**
 * @brief 64 位紧凑分布式全局唯一标识符（雪花算法原语）
 * 布局：[1-bit 保留位] + [41-bit 相对毫秒时间戳] + [10-bit 节点标识] + [12-bit 递增序号]
 */
struct GAMECORE_API FGameDistId {
public:
    static constexpr uint8 SEQUENCE_BITS = 12;
    static constexpr uint8 NODE_ID_BITS = 10;
    static constexpr uint8 TIMESTAMP_BITS = 41;

    static constexpr uint16 MAX_SEQUENCE = (1U << SEQUENCE_BITS) - 1U;
    static constexpr uint16 MAX_NODE_ID = (1U << NODE_ID_BITS) - 1U;
    static constexpr uint64 MAX_TIMESTAMP_DELTA = (1ULL << TIMESTAMP_BITS) - 1ULL;

    static constexpr uint8 NODE_ID_SHIFT = SEQUENCE_BITS;
    static constexpr uint8 TIMESTAMP_SHIFT = SEQUENCE_BITS + NODE_ID_BITS;

public:
    uint64 RawValue = 0;

public:
    constexpr FGameDistId() noexcept = default;

    /**
     * @brief 由 64 位原始无符号整数直接构造标识符
     * @param InRawValue 原始数值
     */
    constexpr explicit FGameDistId(uint64 InRawValue) noexcept
        : RawValue(InRawValue) {
    }

    /**
     * @brief 工厂方法：从原始整数构造
     * @param InRawValue 原始无符号整数
     * @return 分布式唯一 ID
     */
    [[nodiscard]] static constexpr FGameDistId FromRaw(uint64 InRawValue) noexcept {
        return FGameDistId(InRawValue);
    }

    /**
     * @brief 判定标识符是否有效（非 0 即为有效）
     * @return 有效返回 true
     */
    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return RawValue != 0;
    }

    /**
     * @brief 布尔显式类型转换操作符
     * @return 有效返回 true
     */
    constexpr explicit operator bool() const noexcept {
        return IsValid();
    }

    /**
     * @brief 获取底层 64 位裸数值
     * @return 64 位裸数值
     */
    [[nodiscard]] constexpr uint64 GetRawValue() const noexcept {
        return RawValue;
    }

    /**
     * @brief 提取节点标识位段
     * @return 10 位节点编号 [0, 1023]
     */
    [[nodiscard]] constexpr uint16 GetNodeId() const noexcept {
        return static_cast<uint16>((RawValue >> NODE_ID_SHIFT) & MAX_NODE_ID);
    }

    /**
     * @brief 提取毫秒内自增序号位段
     * @return 12 位序号 [0, 4095]
     */
    [[nodiscard]] constexpr uint16 GetSequence() const noexcept {
        return static_cast<uint16>(RawValue & MAX_SEQUENCE);
    }

    /**
     * @brief 提取相对于纪元的相对毫秒偏移量（严格掩码 41 位，忽略最高保留位）
     * @return 41 位相对毫秒数
     */
    [[nodiscard]] constexpr int64 GetTimestampDelta() const noexcept {
        return static_cast<int64>((RawValue >> TIMESTAMP_SHIFT) & MAX_TIMESTAMP_DELTA);
    }

    /**
     * @brief 基于基准纪元反解真实 Unix 毫秒时间戳
     * @param CustomEpochMs 基准纪元 Unix 毫秒时间戳
     * @return 绝对 Unix 毫秒时间戳
     */
    [[nodiscard]] constexpr int64 GetTimestampMilliseconds(int64 CustomEpochMs) const noexcept {
        return CustomEpochMs + GetTimestampDelta();
    }

    /**
     * @brief 转换为易读十进制字符串
     * @return 数字格式化字符串
     */
    [[nodiscard]] FString ToString() const {
        return FString::Printf(TEXT("%llu"), RawValue);
    }

    /**
     * @brief 转换为十六进制字符串格式
     * @return 十六进制字符串（带 0x 前缀，16 位对齐）
     */
    [[nodiscard]] FString ToHex() const {
        return FString::Printf(TEXT("0x%016llX"), RawValue);
    }

    /**
     * @brief 从字符串（十进制或 0x 前缀十六进制）解析唯一 ID
     * @param InStr 待解析字符串切片
     * @param OutId 传出标识符引用
     * @return 解析成功返回 true
     */
    static bool FromString(FStringView InStr, FGameDistId& OutId) noexcept {
        uint64 Parsed = 0;
        if (Game::Private::ParseUInt64FromView(InStr, Parsed)) {
            OutId = FGameDistId(Parsed);
            return true;
        }
        return false;
    }

    /**
     * @brief 从各组成位段合成标识符（若 TimestampDelta 为负或超出 41 位上限则返回无效 ID）
     * @param TimestampDelta 相对纪元的毫秒数 [0, 2^41 - 1]
     * @param NodeId 节点编号
     * @param Sequence 毫秒内序号
     * @return 构造出的分布式标识符
     */
    [[nodiscard]] static constexpr FGameDistId FromParts(int64 TimestampDelta, uint16 NodeId, uint16 Sequence) noexcept {
        if (TimestampDelta < 0 || static_cast<uint64>(TimestampDelta) > MAX_TIMESTAMP_DELTA) {
            return FGameDistId(0ULL);
        }
        const uint64 T = (static_cast<uint64>(TimestampDelta) & MAX_TIMESTAMP_DELTA) << TIMESTAMP_SHIFT;
        const uint64 N = static_cast<uint64>(NodeId & MAX_NODE_ID) << NODE_ID_SHIFT;
        const uint64 S = static_cast<uint64>(Sequence & MAX_SEQUENCE);
        return FGameDistId(T | N | S);
    }

    constexpr bool operator==(const FGameDistId& Other) const noexcept {
        return RawValue == Other.RawValue;
    }

    constexpr bool operator!=(const FGameDistId& Other) const noexcept {
        return RawValue != Other.RawValue;
    }

    constexpr bool operator<(const FGameDistId& Other) const noexcept {
        return RawValue < Other.RawValue;
    }

    constexpr bool operator<=(const FGameDistId& Other) const noexcept {
        return RawValue <= Other.RawValue;
    }

    constexpr bool operator>(const FGameDistId& Other) const noexcept {
        return RawValue > Other.RawValue;
    }

    constexpr bool operator>=(const FGameDistId& Other) const noexcept {
        return RawValue >= Other.RawValue;
    }

    /**
     * @brief 雪崩散列计算函数（彻底消除序号聚集对 TMap / TSet 的性能退化）
     * @param InId 目标 ID
     * @return 均匀分布的 32 位哈希码
     */
    friend uint32 GetTypeHash(const FGameDistId& InId) noexcept {
        return Game::Hash::Hash64To32(InId.RawValue);
    }

    /**
     * @brief 序列化至归档流
     * @param Ar 归档流引用
     * @param InId 目标 ID 引用
     * @return 归档流引用
     */
    friend FArchive& operator<<(FArchive& Ar, FGameDistId& InId) {
        Ar << InId.RawValue;
        return Ar;
    }

    /**
     * @brief 引擎 LexToString 支持
     * @param InId 目标 ID
     * @return 格式化字符串
     */
    friend FString LexToString(const FGameDistId& InId) {
        return InId.ToString();
    }

    /**
     * @brief 引擎 LexFromString 支持
     * @param OutId 传出 ID
     * @param Buffer 字符串缓冲区
     * @return 成功解析返回 true
     */
    friend bool LexFromString(FGameDistId& OutId, const TCHAR* Buffer) {
        if (Buffer == nullptr) {
            return false;
        }
        return FromString(FStringView(Buffer), OutId);
    }
};
