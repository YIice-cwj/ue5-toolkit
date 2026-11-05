/**
 * @file NetHeartbeatKeeper.h
 * @brief 长连接周期心跳保活、RTT 往返时延测算与静默超时侦测器
 */

#pragma once

#include "CoreMinimal.h"
#include "Policy/NetPolicyTypes.h"

/**
 * @brief 长连接心跳保活与对端失步超时侦测器
 */
class GAMENETSERVICE_API FNetHeartbeatKeeper final {
public:
    static constexpr float MIN_INTERVAL_SECONDS = 0.1f;

public:
    /**
     * @brief 构造心跳维持器
     * @param InConfig 心跳间隔、超时阈值及 Ping/Pong 协议 ID 配置
     */
    explicit FNetHeartbeatKeeper(const FNetHeartbeatConfig& InConfig = {}) noexcept;

    /**
     * @brief 更新心跳配置并重置计时状态
     * @param InConfig 新心跳配置
     */
    void SetConfig(const FNetHeartbeatConfig& InConfig) noexcept;

    /**
     * @brief 获取当前心跳配置
     * @return 心跳配置常引用
     */
    [[nodiscard]] const FNetHeartbeatConfig& GetConfig() const noexcept {
        return Config;
    }

    /**
     * @brief 重置所有计时器与挂起的 Ping 采样记录
     */
    void Reset() noexcept;

    /**
     * @brief 推进心跳计时器，判定当前帧是否需发送 Ping 或触发超时断线
     * @param DeltaTimeSeconds 帧间隔秒数
     * @return 本帧应执行的心跳动作枚举
     */
    [[nodiscard]] ENetHeartbeatAction Tick(float DeltaTimeSeconds) noexcept;

    /**
     * @brief 记录已发出的 Ping 序列号与单调时钟时间戳以供计算 RTT
     * @param SequenceId 发出的 Ping 序列号
     * @param SentTimestampSeconds 发出时刻秒数
     */
    void RecordPingSent(uint32 SequenceId, double SentTimestampSeconds) noexcept;

    /**
     * @brief 通知收到任意下行报文以刷新对端存活计时；若命中挂起 Pong 则更新最新 RTT
     * @param MessageId 收到的下行报文协议 ID
     * @param SequenceId 收到的下行报文序列号
     * @param ReceivedTimestampSeconds 收到时刻秒数
     */
    void NotifyInboundPacket(uint32 MessageId, uint32 SequenceId, double ReceivedTimestampSeconds) noexcept;

    /**
     * @brief 查询最近一次成功测算的往返延迟毫秒数
     * @return 最近一次 RTT（毫秒），尚未完成采样时为 0
     */
    [[nodiscard]] float GetLastRttMilliseconds() const noexcept {
        return LastRttMilliseconds;
    }

    /**
     * @brief 查询距离最近一次收到对端任意数据的静默秒数
     * @return 静默持续秒数
     */
    [[nodiscard]] float GetSecondsSinceLastInbound() const noexcept {
        return SecondsSinceLastInbound;
    }

private:
    FNetHeartbeatConfig Config;
    float SecondsSinceLastPing{0.0f};
    float SecondsSinceLastInbound{0.0f};
    uint32 PendingPingSequenceId{0};
    double PendingPingSentSeconds{0.0};
    bool bHasPendingPing{false};
    float LastRttMilliseconds{0.0f};
};

