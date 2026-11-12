// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/FGameLocalId.h"

/**
 * @brief 极速原子无锁本地唯一 ID 生成器
 */
class GAMECORE_API FGameLocalIdGenerator {
public:
    FGameLocalIdGenerator() = delete;

    /**
     * @brief 生成下一个单调递增的本地唯一 ID（线程安全、无等待、松弛原子序）
     * @return 新生成的本地 ID
     */
    [[nodiscard]] static FGameLocalId NextId() noexcept;

    /**
     * @brief 探测当前最新的序号数值（不推进序列号）
     * @return 当前序列值
     */
    [[nodiscard]] static uint64 PeekCurrent() noexcept;

    /**
     * @brief 重置序列生成器起始值（专供自动化测试与世界会话生命周期初始化）
     * @param InitialValue 起始重置值（必须 > 0）
     */
    static void ResetForTesting(uint64 InitialValue = 1ULL) noexcept;
};
