/**
 * @file LanUdpSocketChannel.h
 * @brief 零业务耦合的 IPv4 非阻塞广播 UDP Socket RAII 收发通道
 */

#pragma once

#include "CoreMinimal.h"
#include "LAN/LanBeaconTypes.h"

class FInternetAddr;
class FSocket;

/**
 * @brief 封装 IPv4 非阻塞广播 UDP Socket 打开、关闭、收发与内核可读等待的底层通道
 */
class GAMENETSESSION_API FLanUdpSocketChannel final {
public:
    static constexpr int32 MAX_DATAGRAM_BUFFER_BYTES = 4096 + 64;

public:
    FLanUdpSocketChannel() = default;

    /**
     * @brief 析构时自动关闭并销毁底层 UDP Socket
     */
    ~FLanUdpSocketChannel();

    FLanUdpSocketChannel(const FLanUdpSocketChannel&) = delete;
    FLanUdpSocketChannel& operator=(const FLanUdpSocketChannel&) = delete;
    FLanUdpSocketChannel(FLanUdpSocketChannel&&) = delete;
    FLanUdpSocketChannel& operator=(FLanUdpSocketChannel&&) = delete;

    /**
     * @brief 创建并绑定 IPv4 非阻塞广播 UDP Socket
     * @param BindIp 本地绑定 IP 字符串（支持 "0.0.0.0" 或具体网卡地址）
     * @param BindPort 本地绑定端口号（传 0 由操作系统分配空闲端口）
     * @return 绑定成功返回 true
     */
    bool Open(const FString& BindIp, int32 BindPort);

    /**
     * @brief 关闭并释放当前持有的底层 UDP Socket
     */
    void Close();

    /**
     * @brief 向指定目标 IP 与端口发送一帧完整 UDP 数据报
     * @param PayloadBytes 待发送字节切片
     * @param TargetIp 目标 IPv4 地址（支持 "255.255.255.255" 广播与单播地址）
     * @param TargetPort 目标 UDP 端口（1 ~ 65535）
     * @return 完整写出全部字节返回 true
     */
    bool SendTo(TConstArrayView<uint8> PayloadBytes, const FString& TargetIp, int32 TargetPort);

    /**
     * @brief 非阻塞尝试从接收缓冲队列读取一帧到达的 UDP 数据报
     * @return 有可读数据报时返回包含字节流与来源 IP/端口的值对象，无数据时返回空值
     */
    [[nodiscard]] TOptional<FLanReceivedDatagram> TryReceiveDatagram();

    /**
     * @brief 基于内核 select/poll 事件驱动等待底层 Socket 可读就绪
     * @param MaxWaitSeconds 最大等待秒数上限
     * @return 超时前检测到可读数据报到达返回 true
     */
    bool WaitReadable(double MaxWaitSeconds) const;

    /**
     * @brief 查询底层 UDP Socket 当前是否处于已打开绑定状态
     * @return 已打开返回 true
     */
    [[nodiscard]] bool IsOpen() const noexcept {
        return RawSocket != nullptr;
    }

    /**
     * @brief 获取底层 UDP Socket 实际绑定的本地端口号
     * @return 实际绑定端口号；未打开时返回 0
     */
    [[nodiscard]] int32 GetBoundPort() const noexcept {
        return BoundPort;
    }

private:
    FSocket* RawSocket{nullptr};
    int32 BoundPort{0};
    TSharedPtr<FInternetAddr> CachedSenderAddr;
};

