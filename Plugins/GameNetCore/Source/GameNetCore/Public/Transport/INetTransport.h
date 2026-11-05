/**
 * @file INetTransport.h
 * @brief 网络传输通道驱动抽象契约接口定义
 */

#pragma once

#include "CoreMinimal.h"
#include "Packet/NetPacketTypes.h"
#include "Types/NetConnectionTypes.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnNetConnectionStateChanged, ENetConnectionState);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnNetPacketReceived, const FNetPacketHeader&, TConstArrayView<uint8>);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnNetTransportError, const FString&);

/**
 * @brief 传输驱动核心抽象契约（屏蔽底层 WebSocket / TCP / KCP 与单机内存回环通道差异）
 */
class GAMENETCORE_API INetTransport {
public:
    virtual ~INetTransport() = default;

    /**
     * @brief 启动向目标端点（或本地虚拟回环地址）的连接流程
     * @param Endpoint 目标地址（例如 "wss://gate.example.com/ws" 或 "mock://local"）
     * @return 成功发起连接流程返回 true，端点非法或状态不允许时返回 false
     */
    virtual bool Connect(const FString& Endpoint) = 0;

    /**
     * @brief 主动关闭当前传输连接并清理会话状态
     * @param ReasonCode 关闭状态码（如标准 1000 正常关闭）
     * @param ReasonDetail 关闭原因描述文本
     */
    virtual void Disconnect(int32 ReasonCode, const FString& ReasonDetail) = 0;

    /**
     * @brief 发送一帧完整数据包
     * @param Header 待发送的报文头（PayloadLength 自动对齐实际负载字节数）
     * @param Payload 待发送的负载字节视图
     * @return 成功写入底层发送通道返回 true，未连接或报文头不合法时返回 false
     */
    virtual bool Send(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload) = 0;

    /**
     * @brief 查询当前传输驱动状态机枚举值
     * @return 当前连接状态
     */
    [[nodiscard]] virtual ENetConnectionState GetState() const noexcept = 0;

    /**
     * @brief 游戏主线程帧轮询钩子，用于跨线程安全队列消费与事件派发
     * @param DeltaTimeSeconds 距离上一帧的间隔秒数
     */
    virtual void Tick(float DeltaTimeSeconds) = 0;

    /**
     * @brief 获取连接状态转移多播事件委托
     * @return 状态变更委托引用
     */
    virtual FOnNetConnectionStateChanged& OnStateChanged() = 0;

    /**
     * @brief 获取完整数据包到达多播事件委托
     * @return 数据包到达委托引用
     */
    virtual FOnNetPacketReceived& OnPacketReceived() = 0;

    /**
     * @brief 获取底层通道异常多播事件委托
     * @return 错误事件委托引用
     */
    virtual FOnNetTransportError& OnTransportError() = 0;
};

