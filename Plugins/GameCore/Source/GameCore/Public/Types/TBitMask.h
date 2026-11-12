/**
 * @file TBitMask.h
 * @brief 强类型枚举紧凑位掩码集合模板（带编译期与运行期越界移位防护）
 */

#pragma once

#include "CoreMinimal.h"

#include <type_traits>

/**
 * @brief 强类型枚举紧凑位掩码集合（基于 0 起始的位索引映射至底层二进制比特位）
 *
 * 契约约定：
 * - 构造函数与 Set/Has/Toggle 接收的 TEnum 实参代表【位索引（Bit Index，如 0, 1, 2...）】；
 * - 内部自动将位索引映射为 (1 << BitIndex) 单比特掩码；
 * - 若需直接从原生二进制掩码（Bit Pattern，如 0x01, 0x02, 0x04）初始化，请使用 FromRawMask 工厂方法或 explicit TBitMask(FUnderlying)。
 *
 * @tparam TEnum 枚举类型
 */
template <typename TEnum>
class TBitMask {
    static_assert(std::is_enum_v<TEnum>, "TBitMask 仅支持枚举类型");

public:
    using FUnderlying = std::underlying_type_t<TEnum>;
    using FUnsignedUnderlying = std::make_unsigned_t<FUnderlying>;

    static constexpr size_t BIT_WIDTH = sizeof(FUnderlying) * 8;

public:
    /**
     * @brief 默认构造函数：所有标记位清零
     */
    constexpr TBitMask() noexcept
        : RawMask(0) {
    }

    /**
     * @brief 构造函数：基于单枚举位初始化（越界位自动忽略为 0）
     * @param InBit 初始枚举标记
     */
    constexpr explicit TBitMask(TEnum InBit) noexcept
        : RawMask(MakeSingleBitMask(InBit)) {
    }

    /**
     * @brief 构造函数：基于原始掩码初始化
     * @param InRawMask 原始数值掩码
     */
    constexpr explicit TBitMask(FUnderlying InRawMask) noexcept
        : RawMask(InRawMask) {
    }

    /**
     * @brief 工厂方法：基于单枚举位索引创建位掩码（等价于 1 << BitIndex）
     * @param BitIndex 0 起始的枚举位索引
     * @return 构造出的强类型位掩码实例
     */
    [[nodiscard]] static constexpr TBitMask FromBitIndex(TEnum BitIndex) noexcept {
        return TBitMask(BitIndex);
    }

    /**
     * @brief 工厂方法：基于原始二进制位掩码创建（直接包装底层二进制掩码）
     * @param InRawMask 底层位掩码整数
     * @return 构造出的强类型位掩码实例
     */
    [[nodiscard]] static constexpr TBitMask FromRawMask(FUnderlying InRawMask) noexcept {
        return TBitMask(InRawMask);
    }

    /**
     * @brief 检查是否包含指定枚举标记位
     * @param InBit 目标枚举位
     * @return 包含返回 true；枚举位越界时返回 false
     */
    [[nodiscard]] constexpr bool Has(TEnum InBit) const noexcept {
        const FUnderlying BitVal = MakeSingleBitMask(InBit);
        return BitVal != 0 && (RawMask & BitVal) != 0;
    }

    /**
     * @brief 启用指定枚举标记位（越界位安全忽略）
     * @param InBit 目标枚举位
     * @return 自身引用
     */
    constexpr TBitMask& Set(TEnum InBit) noexcept {
        RawMask |= MakeSingleBitMask(InBit);
        return *this;
    }

    /**
     * @brief 清除指定枚举标记位（越界位安全忽略）
     * @param InBit 目标枚举位
     * @return 自身引用
     */
    constexpr TBitMask& Clear(TEnum InBit) noexcept {
        RawMask &= ~MakeSingleBitMask(InBit);
        return *this;
    }

    /**
     * @brief 翻转指定枚举标记位（越界位安全忽略）
     * @param InBit 目标枚举位
     * @return 自身引用
     */
    constexpr TBitMask& Toggle(TEnum InBit) noexcept {
        RawMask ^= MakeSingleBitMask(InBit);
        return *this;
    }

    /**
     * @brief 检查是否包含另一个掩码中的任意标记位
     * @param Other 另一掩码
     * @return 存在交集返回 true
     */
    [[nodiscard]] constexpr bool HasAny(const TBitMask& Other) const noexcept {
        return (RawMask & Other.RawMask) != 0;
    }

    /**
     * @brief 检查是否包含另一个掩码中的全部标记位
     * @param Other 另一掩码
     * @return 完全包含返回 true
     */
    [[nodiscard]] constexpr bool HasAll(const TBitMask& Other) const noexcept {
        return (RawMask & Other.RawMask) == Other.RawMask;
    }

    /**
     * @brief 检查是否完全不包含另一个掩码中的任何标记位
     * @param Other 另一掩码
     * @return 无任何交集返回 true
     */
    [[nodiscard]] constexpr bool HasNone(const TBitMask& Other) const noexcept {
        return (RawMask & Other.RawMask) == 0;
    }

    /**
     * @brief 检查掩码是否全空
     * @return 全空返回 true
     */
    [[nodiscard]] constexpr bool IsEmpty() const noexcept {
        return RawMask == 0;
    }

    /**
     * @brief 清空所有标记位
     */
    constexpr void Clear() noexcept {
        RawMask = 0;
    }

    /**
     * @brief 获取底层原生掩码值
     * @return 原生掩码整数
     */
    [[nodiscard]] constexpr FUnderlying GetRawMask() const noexcept {
        return RawMask;
    }

    constexpr bool operator==(const TBitMask& Other) const noexcept = default;
    constexpr bool operator!=(const TBitMask& Other) const noexcept = default;

    /**
     * @brief 按位并集运算符
     * @param Other 另一掩码
     * @return 并集掩码
     */
    constexpr TBitMask operator|(const TBitMask& Other) const noexcept {
        return TBitMask(RawMask | Other.RawMask);
    }

    /**
     * @brief 按位交集运算符
     * @param Other 另一掩码
     * @return 交集掩码
     */
    constexpr TBitMask operator&(const TBitMask& Other) const noexcept {
        return TBitMask(RawMask & Other.RawMask);
    }

    /**
     * @brief 按位对称差（异或）运算符
     * @param Other 另一掩码
     * @return 异或掩码
     */
    constexpr TBitMask operator^(const TBitMask& Other) const noexcept {
        return TBitMask(RawMask ^ Other.RawMask);
    }

    /**
     * @brief 按位取反运算符
     * @return 取反掩码
     */
    constexpr TBitMask operator~() const noexcept {
        return TBitMask(static_cast<FUnderlying>(~static_cast<FUnsignedUnderlying>(RawMask)));
    }

private:
    /**
     * @brief 将单个枚举位索引安全转换为底层单比特掩码（防止超过位宽的未定义移位行为）
     * @param InBit 枚举位索引
     * @return 有效位返回 (1 << InBit)，负数或 >= BIT_WIDTH 返回 0
     */
    [[nodiscard]] static constexpr FUnderlying MakeSingleBitMask(TEnum InBit) noexcept {
        const FUnderlying RawIndex = static_cast<FUnderlying>(InBit);
        if constexpr (std::is_signed_v<FUnderlying>) {
            if (RawIndex < 0) {
                return 0;
            }
        }
        const size_t BitIndex = static_cast<size_t>(RawIndex);
        if (BitIndex >= BIT_WIDTH) {
            return 0;
        }
        return static_cast<FUnderlying>(static_cast<FUnsignedUnderlying>(1) << BitIndex);
    }

private:
    FUnderlying RawMask;
};

/**
 * @brief 计算强类型位掩码哈希值（支持直接作为 TMap / TSet 键）
 * @tparam TEnum 关联强类型枚举
 * @param Mask 目标位掩码
 * @return 32 位哈希值
 */
template <typename TEnum>
[[nodiscard]] uint32 GetTypeHash(const TBitMask<TEnum>& Mask) noexcept {
    return GetTypeHash(Mask.GetRawMask());
}
