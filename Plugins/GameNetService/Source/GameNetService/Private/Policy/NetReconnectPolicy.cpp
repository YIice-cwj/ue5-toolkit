// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/NetReconnectPolicy.h"

/**
 * @brief 构造指数退避重连策略器
 * @param InConfig 重连阈值、上限与抖动比例配置
 * @param RandomSeed 确定性随机种子
 */
FNetReconnectPolicy::FNetReconnectPolicy(const FNetReconnectConfig& InConfig, int32 RandomSeed) noexcept
    : Config(InConfig)
    , RandomStream(RandomSeed) {
}

/**
 * @brief 更新重连配置并重置当前计数状态
 * @param InConfig 新重连策略配置
 */
void FNetReconnectPolicy::SetConfig(const FNetReconnectConfig& InConfig) noexcept {
    Config = InConfig;
    Reset();
}

/**
 * @brief 申请下一次重连退避等待时长；达到最大尝试次数或禁用自动重连时触发熔断返回空值
 * @return 计划等待的秒数；已耗尽重试配额时返回空值
 */
TOptional<float> FNetReconnectPolicy::ScheduleNextAttempt() noexcept {
    if (!Config.bEnableAutoReconnect || Config.MaxRetryAttempts <= 0 || CurrentAttempt >= Config.MaxRetryAttempts) {
        bWaitingForRetry = false;
        RemainingDelaySeconds = 0.0f;
        bExhausted = true;
        return NullOpt;
    }

    ++CurrentAttempt;
    const float SafeBaseSeconds = FMath::Max(Config.BaseDelaySeconds, MIN_DELAY_FLOOR_SECONDS);
    const float SafeMaxSeconds = FMath::Max(Config.MaxDelaySeconds, SafeBaseSeconds);
    const int32 ShiftExponent = FMath::Clamp(CurrentAttempt - 1, 0, MAX_BACKOFF_EXPONENT);

    const double ExponentialDelay = static_cast<double>(SafeBaseSeconds) * static_cast<double>(1u << ShiftExponent);
    const float CappedDelay = static_cast<float>(FMath::Min<double>(ExponentialDelay, SafeMaxSeconds));

    const float ClampedJitterRatio = FMath::Clamp(Config.JitterRatio, 0.0f, 1.0f);
    const float RandomFactor = 1.0f + RandomStream.FRandRange(-1.0f, 1.0f) * ClampedJitterRatio;
    const float ScheduledSeconds = FMath::Clamp(CappedDelay * RandomFactor, MIN_DELAY_FLOOR_SECONDS, SafeMaxSeconds);

    RemainingDelaySeconds = ScheduledSeconds;
    bWaitingForRetry = true;
    bExhausted = false;
    return ScheduledSeconds;
}

/**
 * @brief 推进重连倒计时器，倒计时归零时返回 true 通知外部发起重连动作
 * @param DeltaTimeSeconds 距离上一帧的间隔秒数
 * @return 本次推进恰好到达重连触发时刻返回 true，否则返回 false
 */
bool FNetReconnectPolicy::AdvanceTimer(float DeltaTimeSeconds) noexcept {
    if (!bWaitingForRetry || DeltaTimeSeconds <= 0.0f) {
        return false;
    }

    RemainingDelaySeconds -= DeltaTimeSeconds;
    if (RemainingDelaySeconds > 0.0f) {
        return false;
    }

    RemainingDelaySeconds = 0.0f;
    bWaitingForRetry = false;
    return true;
}

/**
 * @brief 重置累计重试次数、倒计时与耗尽熔断标志
 */
void FNetReconnectPolicy::Reset() noexcept {
    CurrentAttempt = 0;
    RemainingDelaySeconds = 0.0f;
    bWaitingForRetry = false;
    bExhausted = false;
}

