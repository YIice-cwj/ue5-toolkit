// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/NetHeartbeatKeeper.h"

/**
 * @brief 构造心跳维持器
 * @param InConfig 心跳间隔、超时阈值及 Ping/Pong 协议 ID 配置
 */
FNetHeartbeatKeeper::FNetHeartbeatKeeper(const FNetHeartbeatConfig& InConfig) noexcept
    : Config(InConfig) {
}

/**
 * @brief 更新心跳配置并重置计时状态
 * @param InConfig 新心跳配置
 */
void FNetHeartbeatKeeper::SetConfig(const FNetHeartbeatConfig& InConfig) noexcept {
    Config = InConfig;
    Reset();
}

/**
 * @brief 重置所有计时器与挂起的 Ping 采样记录
 */
void FNetHeartbeatKeeper::Reset() noexcept {
    SecondsSinceLastPing = 0.0f;
    SecondsSinceLastInbound = 0.0f;
    PendingPingSequenceId = 0;
    PendingPingSentSeconds = 0.0;
    bHasPendingPing = false;
    LastRttMilliseconds = 0.0f;
}

/**
 * @brief 推进心跳计时器，判定当前帧是否需发送 Ping 或触发超时断线
 * @param DeltaTimeSeconds 帧间隔秒数
 * @return 本帧应执行的心跳动作枚举
 */
ENetHeartbeatAction FNetHeartbeatKeeper::Tick(float DeltaTimeSeconds) noexcept {
    if (!Config.bEnabled || DeltaTimeSeconds <= 0.0f) {
        return ENetHeartbeatAction::None;
    }

    const float SafeInterval = FMath::Max(Config.IntervalSeconds, MIN_INTERVAL_SECONDS);
    const float SafeTimeout = FMath::Max(Config.TimeoutSeconds, SafeInterval);

    SecondsSinceLastInbound += DeltaTimeSeconds;
    SecondsSinceLastPing += DeltaTimeSeconds;

    if (SecondsSinceLastInbound >= SafeTimeout) {
        return ENetHeartbeatAction::TimeoutDisconnect;
    }

    if (SecondsSinceLastPing >= SafeInterval) {
        SecondsSinceLastPing = 0.0f;
        return ENetHeartbeatAction::SendPing;
    }

    return ENetHeartbeatAction::None;
}

/**
 * @brief 记录已发出的 Ping 序列号与单调时钟时间戳以供计算 RTT
 * @param SequenceId 发出的 Ping 序列号
 * @param SentTimestampSeconds 发出时刻秒数
 */
void FNetHeartbeatKeeper::RecordPingSent(uint32 SequenceId, double SentTimestampSeconds) noexcept {
    PendingPingSequenceId = SequenceId;
    PendingPingSentSeconds = SentTimestampSeconds;
    bHasPendingPing = true;
}

/**
 * @brief 通知收到任意下行报文以刷新对端存活计时；若命中挂起 Pong 则更新最新 RTT
 * @param MessageId 收到的下行报文协议 ID
 * @param SequenceId 收到的下行报文序列号
 * @param ReceivedTimestampSeconds 收到时刻秒数
 */
void FNetHeartbeatKeeper::NotifyInboundPacket(
    uint32 MessageId,
    uint32 SequenceId,
    double ReceivedTimestampSeconds
) noexcept {
    SecondsSinceLastInbound = 0.0f;
    if (bHasPendingPing && MessageId == Config.PongMessageId && SequenceId == PendingPingSequenceId) {
        const double ElapsedSeconds = FMath::Max(0.0, ReceivedTimestampSeconds - PendingPingSentSeconds);
        LastRttMilliseconds = static_cast<float>(ElapsedSeconds * 1000.0);
        bHasPendingPing = false;
    }
}

