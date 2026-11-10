/**
 * @file FGameDistId.h
 * @brief 64 位分布式单调递增雪花唯一 ID 值类型原语
 */

#pragma once

#include "CoreMinimal.h"

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
        : RawValue(InRawValue) {}

    /**
     * @brief 判定标识符是否有效（非 0 即为有效）
     * @return 有效返回 true
     */
    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return RawValue != 0;
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
     * @brief 转换为易读字符串
     * @return 数字格式化字符串
     */
    [[nodiscard]] FString ToString() const {
        return FString::Printf(TEXT("%llu"), RawValue);
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

    friend uint32 GetTypeHash(const FGameDistId& InId) noexcept {
        return GetTypeHash(InId.RawValue);
    }

    friend FArchive& operator<<(FArchive& Ar, FGameDistId& InId) {
        Ar << InId.RawValue;
        return Ar;
    }
};
