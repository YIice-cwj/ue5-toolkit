/**
 * @file NetPolicyTypes.h
 * @brief 长连接自动重连策略与周期心跳保活配置及动作枚举数据契约
 */

#pragma once

#include "CoreMinimal.h"

/**
 * @brief 心跳保活器单帧轮询决策动作枚举
 */
enum class ENetHeartbeatAction : uint8 {
    None = 0,
    SendPing,
    TimeoutDisconnect
};

/**
 * @brief 指数退避自动重连配置参数值对象
 */
struct GAMENETSERVICE_API FNetReconnectConfig {
    [[nodiscard]] bool operator==(const FNetReconnectConfig& Other) const noexcept = default;

    /**
     * @brief 校验重连配置参数是否合法
     * @return 基础等待秒数大于零、上限大于等于下限、重试上限非负且抖动比例在 [0, 1] 范围内返回 true
     */
    [[nodiscard]] bool IsValid() const noexcept {
        return BaseDelaySeconds > 0.0f && MaxDelaySeconds >= BaseDelaySeconds && MaxRetryAttempts >= 0 && JitterRatio >= 0.0f && JitterRatio <= 1.0f;
    }

    bool bEnableAutoReconnect{true};
    float BaseDelaySeconds{1.0f};
    float MaxDelaySeconds{30.0f};
    int32 MaxRetryAttempts{5};
    float JitterRatio{0.25f};
};

/**
 * @brief 长连接心跳保活与静默超时配置参数值对象
 */
struct GAMENETSERVICE_API FNetHeartbeatConfig {
    [[nodiscard]] bool operator==(const FNetHeartbeatConfig& Other) const noexcept = default;

    /**
     * @brief 校验心跳保活配置参数是否合法
     * @return 间隔大于零、超时阈值严格大于间隔且 Ping/Pong 消息 ID 非零返回 true
     */
    [[nodiscard]] bool IsValid() const noexcept {
        return IntervalSeconds > 0.0f && TimeoutSeconds > IntervalSeconds && PingMessageId != 0 && PongMessageId != 0;
    }

    bool bEnabled{true};
    float IntervalSeconds{15.0f};
    float TimeoutSeconds{45.0f};
    uint32 PingMessageId{1};
    uint32 PongMessageId{2};
};

