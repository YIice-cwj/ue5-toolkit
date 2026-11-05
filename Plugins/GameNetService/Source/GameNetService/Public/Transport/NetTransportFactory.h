/**
 * @file NetTransportFactory.h
 * @brief 网络传输通道实例无状态构造工厂（隔离门面子系统与具体传输驱动实现耦合）
 */

#pragma once

#include "CoreMinimal.h"
#include "Packet/NetPacketTypes.h"
#include "Transport/INetTransport.h"

/**
 * @brief 提供单机内存回环与 WebSocket 长连接抽象传输驱动的标准实例化工厂
 */
class GAMENETSERVICE_API FNetTransportFactory final {
public:
    FNetTransportFactory() = delete;

public:
    /**
     * @brief 创建单机纯内存回环抽象传输驱动实例（零物理 Socket 开销）
     * @return 面向抽象契约的传输驱动共享引用
     */
    [[nodiscard]] static TSharedRef<INetTransport> CreateMockTransport();

    /**
     * @brief 创建引擎原生 WebSocket (WS / WSS) 二进制抽象传输驱动实例
     * @param MaxPayloadBytes 允许接收的单包最大业务负载字节数
     * @return 面向抽象契约的传输驱动共享引用
     */
    [[nodiscard]] static TSharedRef<INetTransport> CreateWebSocketTransport(
        uint32 MaxPayloadBytes = FNetPacketHeader::DEFAULT_MAX_PAYLOAD_BYTES
    );
};
