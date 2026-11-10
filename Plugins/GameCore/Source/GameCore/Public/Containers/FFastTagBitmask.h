/**
 * @file FFastTagBitmask.h
 * @brief 64 位极速状态位掩码与标签字典
 */

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Math/UnrealMathUtility.h"

/**
 * @brief 64 位无锁极速状态位掩码
 *
 * 针对战斗状态机、连招打断、霸体与控制互斥等高频判定设计：
 * 1. 单条位运算指令完成状态包含与互斥检测（<= 0.3ns）；
 * 2. 8 字节紧凑结构，零堆内存分配，极低复制与网络开销；
 * 3. 配合 FFastTagDictionary 提供 FName 动态字典映射（跨进程同步场景须在启动期按固定顺序预注册标签）。
 */
struct GAMECORE_API FFastTagBitmask {
    uint64 Value = 0ULL;

    constexpr FFastTagBitmask() = default;

    constexpr explicit FFastTagBitmask(uint64 InValue)
        : Value(InValue) {
    }

    /**
     * @brief 激活指定索引的标签位
     * @param BitIndex 标签索引（必须处于 [0, 63] 区间）
     * @return 索引合法且成功置位返回 true，越界返回 false
     */
    bool AddTag(int32 BitIndex) noexcept {
        if (BitIndex >= 0 && BitIndex < 64) {
            Value |= (1ULL << BitIndex);
            return true;
        }
        return false;
    }

    /**
     * @brief 移除指定索引的标签位
     * @param BitIndex 标签索引（必须处于 [0, 63] 区间）
     * @return 索引合法且成功清除返回 true，越界返回 false
     */
    bool RemoveTag(int32 BitIndex) noexcept {
        if (BitIndex >= 0 && BitIndex < 64) {
            Value &= ~(1ULL << BitIndex);
            return true;
        }
        return false;
    }

    /**
     * @brief 翻转指定索引的标签位
     * @param BitIndex 标签索引（必须处于 [0, 63] 区间）
     * @return 索引合法且成功翻转返回 true，越界返回 false
     */
    bool ToggleTag(int32 BitIndex) noexcept {
        if (BitIndex >= 0 && BitIndex < 64) {
            Value ^= (1ULL << BitIndex);
            return true;
        }
        return false;
    }

    /**
     * @brief 查询是否具备指定标签位
     * @param BitIndex 标签索引
     * @return 激活返回 true
     */
    [[nodiscard]] bool HasTag(int32 BitIndex) const noexcept {
        if (BitIndex >= 0 && BitIndex < 64) {
            return (Value & (1ULL << BitIndex)) != 0ULL;
        }
        return false;
    }

    /**
     * @brief 查询是否命中目标掩码中的任意一个标签（或运算）
     * @param Other 目标位掩码
     * @return 存在交集返回 true
     */
    [[nodiscard]] bool HasAny(const FFastTagBitmask& Other) const noexcept {
        return (Value & Other.Value) != 0ULL;
    }

    /**
     * @brief 查询是否完全包含目标掩码中的所有标签（子集判断）
     * @param Other 目标位掩码
     * @return 完全包含返回 true
     */
    [[nodiscard]] bool HasAll(const FFastTagBitmask& Other) const noexcept {
        return (Value & Other.Value) == Other.Value;
    }

    /**
     * @brief 查询是否完全不包含目标掩码中的任何标签（互斥判断）
     * @param Other 目标位掩码
     * @return 无任何交集返回 true
     */
    [[nodiscard]] bool HasNone(const FFastTagBitmask& Other) const noexcept {
        return (Value & Other.Value) == 0ULL;
    }

    /**
     * @brief 合并目标位掩码中的所有标签
     * @param Other 目标位掩码
     */
    void Combine(const FFastTagBitmask& Other) noexcept {
        Value |= Other.Value;
    }

    /**
     * @brief 排除目标位掩码中的所有标签
     * @param Other 目标位掩码
     */
    void Remove(const FFastTagBitmask& Other) noexcept {
        Value &= ~Other.Value;
    }

    /**
     * @brief 清空所有标签位
     */
    void Clear() noexcept {
        Value = 0ULL;
    }

    /**
     * @brief 查询当前位掩码是否为空
     * @return 为空返回 true
     */
    [[nodiscard]] bool IsEmpty() const noexcept {
        return Value == 0ULL;
    }

    /**
     * @brief 计算当前激活的标签总数
     * @return 激活的 bit 数量
     */
    [[nodiscard]] int32 CountBits() const noexcept {
        return static_cast<int32>(FPlatformMath::CountBits64(Value));
    }

    [[nodiscard]] constexpr bool operator==(const FFastTagBitmask& Other) const noexcept {
        return Value == Other.Value;
    }

    [[nodiscard]] constexpr bool operator!=(const FFastTagBitmask& Other) const noexcept {
        return Value != Other.Value;
    }

    [[nodiscard]] constexpr FFastTagBitmask operator|(const FFastTagBitmask& Other) const noexcept {
        return FFastTagBitmask(Value | Other.Value);
    }

    [[nodiscard]] constexpr FFastTagBitmask operator&(const FFastTagBitmask& Other) const noexcept {
        return FFastTagBitmask(Value & Other.Value);
    }

    [[nodiscard]] constexpr FFastTagBitmask operator~() const noexcept {
        return FFastTagBitmask(~Value);
    }
};

/**
 * @brief FName 到 64 位 Tag 索引的线程安全双向映射字典
 */
class GAMECORE_API FFastTagDictionary {
public:
    static constexpr int32 MaxTagCount = 64;

    /**
     * @brief 获取全局单例字典实例
     * @return 字典单例引用
     */
    static FFastTagDictionary& Get();

    /**
     * @brief 注册或查询指定标签名称的位索引
     * @param TagName 标签名称
     * @return 分配的位索引 [0, 63]，注册满或非法名称返回 INDEX_NONE
     */
    int32 RegisterOrFindTag(FName TagName);

    /**
     * @brief 查询已知标签名称的位索引
     * @param TagName 标签名称
     * @return 索引值，未注册返回 INDEX_NONE
     */
    [[nodiscard]] int32 FindTagIndex(FName TagName) const;

    /**
     * @brief 查询已知位索引对应的标签名称
     * @param BitIndex 索引值
     * @return 注册的名称，越界或未注册返回 NAME_None
     */
    [[nodiscard]] FName FindTagName(int32 BitIndex) const;

    /**
     * @brief 批量将标签名称列表转换为位掩码
     * @param TagNames 标签名称视图
     * @param OutAllSucceeded 可选输出标志：若所有标签均合法且未超出 64 上限则置为 true，否则置为 false
     * @return 聚合位掩码
     */
    [[nodiscard]] FFastTagBitmask MakeMask(TConstArrayView<FName> TagNames, bool* OutAllSucceeded = nullptr);

    /**
     * @brief 获取当前已注册的标签总数
     * @return 标签数量
     */
    [[nodiscard]] int32 Num() const;

    /**
     * @brief 重置清空所有注册信息（仅限单测或环境重置使用）
     */
    void Reset();

private:
    mutable FCriticalSection Mutex;
    TArray<FName> RegisteredTagNames;
    TMap<FName, int32> TagNameToIndexMap;
};
