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
    bool bEnabled{true};
    float IntervalSeconds{15.0f};
    float TimeoutSeconds{45.0f};
    uint32 PingMessageId{1};
    uint32 PongMessageId{2};
};

