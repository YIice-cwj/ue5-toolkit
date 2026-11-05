// Copyright Epic Games, Inc. All Rights Reserved.

#include "Transport/NetTransportFactory.h"

#include "Transport/MockNetTransport.h"
#include "Transport/WebSocketNetTransport.h"

/**
 * @brief 创建单机纯内存回环抽象传输驱动实例（零物理 Socket 开销）
 * @return 面向抽象契约的传输驱动共享引用
 */
TSharedRef<INetTransport> FNetTransportFactory::CreateMockTransport() {
    return MakeShared<FMockNetTransport>();
}

/**
 * @brief 创建引擎原生 WebSocket (WS / WSS) 二进制抽象传输驱动实例
 * @param MaxPayloadBytes 允许接收的单包最大业务负载字节数
 * @return 面向抽象契约的传输驱动共享引用
 */
TSharedRef<INetTransport> FNetTransportFactory::CreateWebSocketTransport(uint32 MaxPayloadBytes) {
    return MakeShared<FWebSocketNetTransport>(MaxPayloadBytes);
}
