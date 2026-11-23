// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/NetReconnectPolicy.h"

#include <atomic>

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Logging/GameLogCategories.h"

/**
 * @brief 使用运行期高熵随机种子构造指数退避重连策略器
 * @param InConfig 重连阈值、上限与抖动比例配置
 */
FNetReconnectPolicy::FNetReconnectPolicy(const FNetReconnectConfig& InConfig) noexcept
    : FNetReconnectPolicy(InConfig, GenerateRuntimeEntropySeed()) {
}

/**
 * @brief 使用指定确定性种子构造指数退避重连策略器
 * @param InConfig 重连阈值、上限与抖动比例配置
 * @param DeterministicSeed 确定性随机种子
 */
FNetReconnectPolicy::FNetReconnectPolicy(const FNetReconnectConfig& InConfig, uint64 DeterministicSeed) noexcept
    : Config(InConfig)
    , RandomStream(DeterministicSeed) {
}

/**
 * @brief 更新重连配置并重置当前计数状态
 * @param InConfig 新重连策略配置
 */
void FNetReconnectPolicy::SetConfig(const FNetReconnectConfig& InConfig) noexcept {
    if (InConfig.bEnableAutoReconnect && !InConfig.IsValid()) {
        GAME_LOG(
            Warning,
            TEXT("[Reconnect] 收到非标准重连配置，运行时将自动钳制安全边界: Base=%.2fs, Max=%.2fs, Retries=%d, Jitter=%.2f"),
            InConfig.BaseDelaySeconds,
            InConfig.MaxDelaySeconds,
            InConfig.MaxRetryAttempts,
            InConfig.JitterRatio
        );
    }
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

/**
 * @brief 生成结合时钟周期、进程号与单调盐值的运行期随机种子
 * @return 非确定性 64 位随机种子
 */
uint64 FNetReconnectPolicy::GenerateRuntimeEntropySeed() noexcept {
    static std::atomic<uint64> EntropySalt{0x9E3779B97F4A7C15ULL};
    const uint64 Salt = EntropySalt.fetch_add(0x9E3779B97F4A7C15ULL, std::memory_order_relaxed);
    const uint64 Cycles = FPlatformTime::Cycles64();
    const uint64 Pid = static_cast<uint64>(FPlatformProcess::GetCurrentProcessId());
    uint64 Mixed = Cycles ^ (Pid << 32) ^ Salt;
    Mixed ^= (Mixed >> 30);
    Mixed *= 0xBF58476D1CE4E5B9ULL;
    Mixed ^= (Mixed >> 27);
    Mixed *= 0x94D049BB133111EBULL;
    Mixed ^= (Mixed >> 31);
    return Mixed != 0ULL ? Mixed : 0x517CC1B727220A95ULL;
}
