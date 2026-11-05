/**
 * @file NetReconnectPolicy.h
 * @brief 带双向随机抖动（Jitter）的指数退避自动重连状态机
 */

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Policy/NetPolicyTypes.h"

/**
 * @brief 指数退避与防雪崩随机抖动重连调度策略
 */
class GAMENETSERVICE_API FNetReconnectPolicy final {
public:
    static constexpr float MIN_DELAY_FLOOR_SECONDS = 0.01f;
    static constexpr int32 MAX_BACKOFF_EXPONENT = 20;

public:
    /**
     * @brief 构造指数退避重连策略器
     * @param InConfig 重连阈值、上限与抖动比例配置
     * @param RandomSeed 确定性随机种子（便于自动化测试精确重现抖动序列）
     */
    explicit FNetReconnectPolicy(const FNetReconnectConfig& InConfig = {}, int32 RandomSeed = 1337) noexcept;

    /**
     * @brief 更新重连配置并重置当前计数状态
     * @param InConfig 新重连策略配置
     */
    void SetConfig(const FNetReconnectConfig& InConfig) noexcept;

    /**
     * @brief 读取当前生效的重连配置
     * @return 配置常引用
     */
    [[nodiscard]] const FNetReconnectConfig& GetConfig() const noexcept {
        return Config;
    }

    /**
     * @brief 申请下一次重连退避等待时长；达到最大尝试次数或禁用自动重连时触发熔断返回空值
     * @return 计划等待的秒数；已耗尽重试配额时返回空值
     */
    [[nodiscard]] TOptional<float> ScheduleNextAttempt() noexcept;

    /**
     * @brief 推进重连倒计时器，倒计时归零时返回 true 通知外部发起重连动作
     * @param DeltaTimeSeconds 距离上一帧的间隔秒数
     * @return 本次推进恰好到达重连触发时刻返回 true，否则返回 false
     */
    [[nodiscard]] bool AdvanceTimer(float DeltaTimeSeconds) noexcept;

    /**
     * @brief 重置累计重试次数、倒计时与耗尽熔断标志（连接恢复或主动断开时调用）
     */
    void Reset() noexcept;

    /**
     * @brief 查询当前已累计调度的重连尝试次数
     * @return 当前尝试序号（未发生重连时为 0）
     */
    [[nodiscard]] int32 GetCurrentAttempt() const noexcept {
        return CurrentAttempt;
    }

    /**
     * @brief 查询剩余退避等待秒数
     * @return 剩余等待秒数
     */
    [[nodiscard]] float GetRemainingDelaySeconds() const noexcept {
        return RemainingDelaySeconds;
    }

    /**
     * @brief 查询当前是否正处于退避倒计时等待阶段
     * @return 正在等待重连触发返回 true
     */
    [[nodiscard]] bool IsWaitingForRetry() const noexcept {
        return bWaitingForRetry;
    }

    /**
     * @brief 查询重连配额是否已耗尽熔断
     * @return 已耗尽最大重试次数返回 true
     */
    [[nodiscard]] bool IsExhausted() const noexcept {
        return bExhausted;
    }

private:
    FNetReconnectConfig Config;
    FRandomStream RandomStream;
    int32 CurrentAttempt{0};
    float RemainingDelaySeconds{0.0f};
    bool bWaitingForRetry{false};
    bool bExhausted{false};
};

