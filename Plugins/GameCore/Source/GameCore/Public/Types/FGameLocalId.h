/**
 * @file FGameLocalId.h
 * @brief 极速原子无锁 64 位本地自增唯一 ID（雪崩散列 · 字符串双向序列化 · 与 FGameDistId 零成本互通）
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/StringView.h"
#include "Types/FGameDistId.h"

/**
 * @brief 64 位本地自增唯一标识符
 *
 * 与分布式雪花 ID（FGameDistId）形成对称设计：
 * - FGameDistId：专司多服网络、跨节点协同与持久化记录（带时间戳与节点号）；
 * - FGameLocalId：专司进程内、单机与客户端运行时会话（纯 64 位无锁原子递增，每秒可产数亿个，零时间戳/节点开销）。
 */
struct GAMECORE_API FGameLocalId {
    /**
     * @brief 64 位原始数字标识
     */
    uint64 Value = 0ULL;

    constexpr FGameLocalId() noexcept = default;

    constexpr explicit FGameLocalId(uint64 InValue) noexcept
        : Value(InValue) {
    }

    /**
     * @brief 工厂方法：从原始整数构造
     * @param InValue 原始数值
     * @return 本地唯一 ID
     */
    [[nodiscard]] static constexpr FGameLocalId FromRaw(uint64 InValue) noexcept {
        return FGameLocalId(InValue);
    }

    /**
     * @brief 从分布式雪花 ID 直接映射构造本地 ID
     * @param InDistId 分布式雪花 ID
     * @return 映射后的本地 ID
     */
    [[nodiscard]] static constexpr FGameLocalId FromDistId(const FGameDistId& InDistId) noexcept {
        return FGameLocalId(InDistId.GetRawValue());
    }

    /**
     * @brief 判定 ID 是否有效（非 0）
     * @return 有效返回 true
     */
    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return Value != 0ULL;
    }

    /**
     * @brief 布尔显式类型转换操作符
     * @return 有效返回 true
     */
    constexpr explicit operator bool() const noexcept {
        return IsValid();
    }

    /**
     * @brief 获取 64 位底层整数
     * @return 原始整数值
     */
    [[nodiscard]] constexpr uint64 GetValue() const noexcept {
        return Value;
    }

    /**
     * @brief 将本地 ID 映射包装为 FGameDistId
     * @return 对应的分布式雪花 ID
     */
    [[nodiscard]] constexpr FGameDistId ToDistId() const noexcept {
        return FGameDistId(Value);
    }

    /**
     * @brief 转换为易读十进制字符串
     * @return 数字格式化字符串
     */
    [[nodiscard]] FString ToString() const {
        return FString::Printf(TEXT("%llu"), Value);
    }

    /**
     * @brief 转换为十六进制字符串格式
     * @return 十六进制字符串（带 0x 前缀，16 位对齐）
     */
    [[nodiscard]] FString ToHex() const {
        return FString::Printf(TEXT("0x%016llX"), Value);
    }

    /**
     * @brief 从字符串（十进制或 0x 前缀十六进制）解析本地唯一 ID
     * @param InStr 待解析字符串切片
     * @param OutId 传出标识符引用
     * @return 解析成功返回 true
     */
    static bool FromString(FStringView InStr, FGameLocalId& OutId) noexcept {
        uint64 Parsed = 0;
        if (Game::Private::ParseUInt64FromView(InStr, Parsed)) {
            OutId = FGameLocalId(Parsed);
            return true;
        }
        return false;
    }

    [[nodiscard]] constexpr bool operator==(const FGameLocalId& Other) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const FGameLocalId& Other) const noexcept = default;

    /**
     * @brief 雪崩散列计算函数（彻底消除连续自增序列在 TMap / TSet 中的哈希聚集）
     * @param InId 目标 ID
     * @return 均匀分布的 32 位哈希码
     */
    friend uint32 GetTypeHash(const FGameLocalId& InId) noexcept {
        return Game::Hash::Hash64To32(InId.Value);
    }

    /**
     * @brief 序列化至归档流
     * @param Ar 归档流引用
     * @param InId 目标 ID 引用
     * @return 归档流引用
     */
    friend FArchive& operator<<(FArchive& Ar, FGameLocalId& InId) {
        Ar << InId.Value;
        return Ar;
    }

    /**
     * @brief 引擎 LexToString 支持
     * @param InId 目标 ID
     * @return 格式化字符串
     */
    friend FString LexToString(const FGameLocalId& InId) {
        return InId.ToString();
    }

    /**
     * @brief 引擎 LexFromString 支持
     * @param OutId 传出 ID
     * @param Buffer 字符串缓冲区
     * @return 成功解析返回 true
     */
    friend bool LexFromString(FGameLocalId& OutId, const TCHAR* Buffer) {
        if (Buffer == nullptr) {
            return false;
        }
        return FromString(FStringView(Buffer), OutId);
    }
};
