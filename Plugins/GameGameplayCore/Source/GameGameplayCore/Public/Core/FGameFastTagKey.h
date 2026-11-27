/**
 * @file FGameFastTagKey.h
 * @brief 运行期零字符串比较的 GameplayTag 排序键包装器（配合 GameCore::TSortedVectorMap 使用）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

/**
 * @brief 基于 FName::FastLess 的进程内极速排序键（避免 FGameplayTag::operator< 触发 FName 字符串表词典序比对）
 *
 * 仅适用于进程运行期内存容器（如 TSortedVectorMap）；严禁跨进程或持久化依赖其排序结果。
 */
struct FGameFastTagKey {
    FGameplayTag Tag;

    FGameFastTagKey() = default;

    /**
     * @brief 从 GameplayTag 隐式构造极速排序键
     * @param InTag 目标标签
     */
    FORCEINLINE FGameFastTagKey(const FGameplayTag& InTag) noexcept
        : Tag(InTag) {
    }

    /**
     * @brief 基于 FName 整型比较索引的严格弱序小于运算（零字符串表查找）
     * @param Other 待比较键
     * @return 当前键整型序小于对方返回 true
     */
    [[nodiscard]] FORCEINLINE bool operator<(const FGameFastTagKey& Other) const noexcept {
        return Tag.GetTagName().FastLess(Other.Tag.GetTagName());
    }

    /**
     * @brief 精确相等比较运算（基于 FName 单指令整型相等判定，与 FastLess 严格弱序完全同构，含空标签自反等价）
     * @param Other 待比较键
     * @return 标签名称完全相同返回 true
     */
    [[nodiscard]] FORCEINLINE bool operator==(const FGameFastTagKey& Other) const noexcept {
        return Tag.GetTagName() == Other.Tag.GetTagName();
    }
};
