// Copyright Epic Games, Inc. All Rights Reserved.

#include "Types/FGameLocalIdGenerator.h"
#include <atomic>

namespace {

/**
 * @brief 全局原子序列递增计数器（初值从 1 开始，保留 0 作为无效 ID）
 */
std::atomic<uint64> GLocalIdCounter{1ULL};

}  // namespace

/**
 * @brief 生成下一个单调递增的本地唯一 ID（线程安全、无等待、松弛原子序）
 * @return 新生成的本地 ID
 */
FGameLocalId FGameLocalIdGenerator::NextId() noexcept {
    return FGameLocalId{GLocalIdCounter.fetch_add(1ULL, std::memory_order_relaxed)};
}

/**
 * @brief 探测当前最新的序号数值（不推进序列号）
 * @return 当前序列值
 */
uint64 FGameLocalIdGenerator::PeekCurrent() noexcept {
    return GLocalIdCounter.load(std::memory_order_relaxed);
}

/**
 * @brief 重置序列生成器起始值（专供自动化测试与世界会话生命周期初始化）
 * @param InitialValue 起始重置值（必须 > 0）
 */
void FGameLocalIdGenerator::ResetForTesting(uint64 InitialValue) noexcept {
    GLocalIdCounter.store((InitialValue > 0ULL) ? InitialValue : 1ULL, std::memory_order_relaxed);
}
