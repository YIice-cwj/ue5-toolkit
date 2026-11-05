/**
 * @file MockNetTransport.h
 * @brief 单机与离线单元测试专用纯内存回环传输驱动（零物理 Socket 开销）
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Transport/INetTransport.h"

/**
 * @brief 单机/离线纯内存回环传输驱动，使单机模式与联网模式共享 100% 一致的业务调用链路
 */
class GAMENETCORE_API FMockNetTransport final : public INetTransport {
public:
    using FMockRequestHandler = TFunction<TOptional<TArray<uint8>>(const FNetPacketHeader&, TConstArrayView<uint8>)>;

private:
    struct FRegisteredMockRoute {
        uint32 ResponseMessageId{0};
        FMockRequestHandler Handler;
    };

public:
    FMockNetTransport() = default;
    ~FMockNetTransport() override = default;

    FMockNetTransport(const FMockNetTransport&) = delete;
    FMockNetTransport& operator=(const FMockNetTransport&) = delete;
    FMockNetTransport(FMockNetTransport&&) = delete;
    FMockNetTransport& operator=(FMockNetTransport&&) = delete;

    /**
     * @brief 建立内存虚拟回环连接（受 SetAcceptConnections 开关控制）
     * @param Endpoint 非空虚拟端点标识（如 "mock://standalone"）
     * @return 连接成功返回 true，端点为空或拒绝连接时返回 false
     */
    bool Connect(const FString& Endpoint) override;

    /**
     * @brief 断开内存虚拟连接并清空待派发回环队列
     * @param ReasonCode 断开原因码
     * @param ReasonDetail 断开原因说明
     */
    void Disconnect(int32 ReasonCode, const FString& ReasonDetail) override;

    /**
     * @brief 向内存虚拟路由投递请求包；命中模拟处理器或开启回声时入队响应，在下一轮 Tick 主线程派发
     * @param Header 报文头（自动对齐 PayloadLength）
     * @param Payload 负载字节切片
     * @return 已连接且报文头合法时返回 true，否则返回 false
     */
    bool Send(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload) override;

    /**
     * @brief 查询当前内存虚拟连接状态
     * @return 当前连接状态枚举
     */
    [[nodiscard]] ENetConnectionState GetState() const noexcept override {
        return CurrentState;
    }

    /**
     * @brief 消费内存待派发响应队列并在当前主线程触发 OnPacketReceived 广播
     * @param DeltaTimeSeconds 帧间隔秒数
     */
    void Tick(float DeltaTimeSeconds) override;

    /**
     * @brief 获取连接状态变更事件委托
     * @return 状态变更委托引用
     */
    FOnNetConnectionStateChanged& OnStateChanged() override {
        return StateChangedEvent;
    }

    /**
     * @brief 获取数据包到达事件委托
     * @return 数据包到达委托引用
     */
    FOnNetPacketReceived& OnPacketReceived() override {
        return PacketReceivedEvent;
    }

    /**
     * @brief 获取底层错误事件委托
     * @return 错误事件委托引用
     */
    FOnNetTransportError& OnTransportError() override {
        return TransportErrorEvent;
    }

    /**
     * @brief 注册针对特定请求协议 ID 的单机本地模拟处理器（自动继承请求 SequenceId）
     * @param RequestMessageId 客户端发送的请求协议 ID
     * @param ResponseMessageId 回传响应的协议 ID
     * @param Handler 请求处理闭包，返回响应字节负载；返回 NullOpt 表示仅消费不回包
     */
    void RegisterMockHandler(uint32 RequestMessageId, uint32 ResponseMessageId, FMockRequestHandler Handler);

    /**
     * @brief 移除指定请求协议 ID 的本地模拟处理器
     * @param RequestMessageId 待移除的请求协议 ID
     */
    void UnregisterMockHandler(uint32 RequestMessageId);

    /**
     * @brief 清空所有已注册的本地模拟处理器
     */
    void ClearMockHandlers();

    /**
     * @brief 设置未命中任何模拟处理器的报文是否原样回声返回（便于通用通道回环测试）
     * @param bEnable 为 true 时未注册消息原样回推
     */
    void SetEchoUnregisteredMessages(bool bEnable) noexcept {
        bEchoUnregistered = bEnable;
    }

    /**
     * @brief 设置是否允许建立新连接（用于测试连接拒绝、重连退避等异常工况）
     * @param bAccept 为 true 时允许连接，为 false 时 Connect 触发错误并保持断开
     */
    void SetAcceptConnections(bool bAccept) noexcept {
        bAcceptConnections = bAccept;
    }

    /**
     * @brief 主动模拟服务端向客户端推送一条下行消息（入队并在 Tick 时派发）
     * @param MessageId 下行推送协议 ID
     * @param Payload 推送负载字节切片
     * @param SequenceId 序列号（默认 0 表示主动推送通知）
     * @return 当前处于已连接状态且入队成功时返回 true
     */
    bool SimulateServerPush(uint32 MessageId, TConstArrayView<uint8> Payload, uint32 SequenceId = 0);

    /**
     * @brief 模拟远端服务器主动踢下线或网络闪断
     * @param ErrorDetail 断线错误详情描述
     */
    void SimulateRemoteDisconnect(const FString& ErrorDetail);

    /**
     * @brief 查询累计成功通过 Send 投递的上行包数量（供诊断与测试核验）
     * @return 已发送上行包计数
     */
    [[nodiscard]] int32 GetSentPacketCount() const noexcept {
        return SentPacketCount;
    }

    /**
     * @brief 查询最近一次成功连接的端点地址字符串
     * @return 端点地址字符串常引用
     */
    [[nodiscard]] const FString& GetConnectedEndpoint() const noexcept {
        return ConnectedEndpoint;
    }

private:
    /**
     * @brief 变更连接状态并在状态实际发生变化时触发广播
     * @param NewState 目标新状态
     */
    void TransitionState(ENetConnectionState NewState);

private:
    ENetConnectionState CurrentState{ENetConnectionState::Disconnected};
    FString ConnectedEndpoint;
    bool bEchoUnregistered{false};
    bool bAcceptConnections{true};
    int32 SentPacketCount{0};
    TMap<uint32, FRegisteredMockRoute> MockRoutes;
    TQueue<FNetPacket, EQueueMode::Mpsc> PendingInboundQueue;
    FOnNetConnectionStateChanged StateChangedEvent;
    FOnNetPacketReceived PacketReceivedEvent;
    FOnNetTransportError TransportErrorEvent;
};

