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
     * @brief 配置重试间隔与次数上限（不重置已累计次数）
     * @param InIntervalSeconds 重试间隔（秒）
     * @param InMaxRetries 允许的最大重试次数
     */
    void Configure(float InIntervalSeconds, int32 InMaxRetries);

    /**
     * @brief 累计一次尝试并判定是否仍允许重试
     * @return 累计次数未超过上限返回 true
     */
    [[nodiscard]] bool Advance();

    /**
     * @brief 重置已累计次数（配置保持不变）
     */
    void Reset();

    /**
     * @brief 获取已累计的尝试次数
     * @return 尝试次数
     */
    [[nodiscard]] int32 GetAttempt() const;

    /**
     * @brief 获取允许的最大重试次数
     * @return 次数上限
     */
    [[nodiscard]] int32 GetMaxRetries() const;

    /**
     * @brief 获取重试间隔
     * @return 间隔秒数
     */
    [[nodiscard]] float GetIntervalSeconds() const;

private:
    float IntervalSeconds = 0.05f;
    int32 MaxRetries = 0;
    int32 Attempt = 0;
};
