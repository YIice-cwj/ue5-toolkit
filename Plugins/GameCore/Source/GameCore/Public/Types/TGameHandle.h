/**
 * @file TGameHandle.h
 * @brief 64 位强类型代际句柄（32 位槽位索引 + 32 位代际号，防异构混用与悬垂野引用）
 */

#pragma once

#include "CoreMinimal.h"

/**
 * @brief 强类型代际句柄模板（不同 TTag 之间编译期类型隔离，零运行时开销）
 * @tparam TTag 领域幻影标签类型
 */
template <typename TTag>
struct TGameHandle {
public:
    using TagType = TTag;

public:
    static constexpr uint32 INVALID_INDEX = MAX_uint32;

public:
    constexpr TGameHandle() noexcept = default;

    /**
     * @brief 按槽位索引与代际号构造句柄
     * @param InIndex 槽位索引
     * @param InGeneration 代际号（非零有效）
     */
    constexpr TGameHandle(uint32 InIndex, uint32 InGeneration) noexcept
        : Index(InIndex)
        , Generation(InGeneration) {
    }

    /**
     * @brief 检查句柄格式是否有效（索引有效且代际号非零）
     * @return 格式有效返回 true
     */
    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return Index != INVALID_INDEX && Generation != 0u;
    }

    /**
     * @brief 布尔显式转换操作符
     * @return 有效返回 true
     */
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return IsValid();
    }

    /**
     * @brief 获取槽位索引
     * @return 32 位槽位索引
     */
    [[nodiscard]] constexpr uint32 GetIndex() const noexcept {
        return Index;
    }

    /**
     * @brief 获取代际号
     * @return 32 位代际号
     */
    [[nodiscard]] constexpr uint32 GetGeneration() const noexcept {
        return Generation;
    }

    /**
     * @brief 打包为紧凑 64 位无符号整数（高 32 位代际号，低 32 位槽位索引）
     * @return 64 位整数标识
     */
    [[nodiscard]] constexpr uint64 PackUInt64() const noexcept {
        return (static_cast<uint64>(Generation) << 32u) | static_cast<uint64>(Index);
    }

    /**
     * @brief 重置为无效空句柄
     */
    constexpr void Reset() noexcept {
        Index = INVALID_INDEX;
        Generation = 0u;
    }

    /**
     * @brief 相等性比较操作符
     * @param Other 待比较句柄
     * @return 索引与代际均相等返回 true
     */
    [[nodiscard]] constexpr bool operator==(const TGameHandle& Other) const noexcept = default;

public:
    /**
     * @brief 从 64 位打包整数还原代际句柄
     * @param Packed 64 位整数标识
     * @return 还原后的强类型句柄
     */
    [[nodiscard]] static constexpr TGameHandle FromPackedUInt64(uint64 Packed) noexcept {
        return TGameHandle(static_cast<uint32>(Packed & 0xFFFFFFFFULL), static_cast<uint32>(Packed >> 32u));
    }

private:
    uint32 Index = INVALID_INDEX;
    uint32 Generation = 0u;
};

/**
 * @brief 计算代际句柄哈希值（支持直接作为 TMap / TSet 键）
 * @tparam TTag 领域幻影标签类型
 * @param Handle 目标句柄
 * @return 32 位哈希值
 */
template <typename TTag>
[[nodiscard]] uint32 GetTypeHash(const TGameHandle<TTag>& Handle) noexcept {
    return GetTypeHash(Handle.PackUInt64());
}
