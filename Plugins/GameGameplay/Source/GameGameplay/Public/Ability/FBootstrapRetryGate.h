/**
 * @file FBootstrapRetryGate.h
 * @brief GAS 引导限次重试闸门（纯 C++ · 次数上限与重试间隔配置）
 */

#pragma once

#include "CoreMinimal.h"

/**
 * @brief 限次重试闸门
 */
struct GAMEGAMEPLAY_API FBootstrapRetryGate {
    /**
     * @brief 配置重试间隔与次数上限（不重置已累计次数 · 强制内联）
     * @param InIntervalSeconds 重试间隔（秒）
     * @param InMaxRetries 允许的最大重试次数
     */
    FORCEINLINE void Configure(float InIntervalSeconds, int32 InMaxRetries) noexcept {
        IntervalSeconds = InIntervalSeconds;
        MaxRetries = InMaxRetries;
    }

    /**
     * @brief 累计一次尝试并判定是否仍允许重试（强制内联）
     * @return 累计次数未超过上限返回 true
     */
    [[nodiscard]] FORCEINLINE bool Advance() noexcept {
        ++Attempt;
        return Attempt <= MaxRetries;
    }

    /**
     * @brief 重置已累计次数（配置保持不变 · 强制内联）
     */
    FORCEINLINE void Reset() noexcept {
        Attempt = 0;
    }

    /**
     * @brief 获取已累计的尝试次数（强制内联）
     * @return 尝试次数
     */
    [[nodiscard]] FORCEINLINE int32 GetAttempt() const noexcept {
        return Attempt;
    }

    /**
     * @brief 获取允许的最大重试次数（强制内联）
     * @return 次数上限
     */
    [[nodiscard]] FORCEINLINE int32 GetMaxRetries() const noexcept {
        return MaxRetries;
    }

    /**
     * @brief 获取重试间隔（强制内联）
     * @return 间隔秒数
     */
    [[nodiscard]] FORCEINLINE float GetIntervalSeconds() const noexcept {
        return IntervalSeconds;
    }

private:
    float IntervalSeconds = 0.05f;
    int32 MaxRetries = 0;
    int32 Attempt = 0;
};
