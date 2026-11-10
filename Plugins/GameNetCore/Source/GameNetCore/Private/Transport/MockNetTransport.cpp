// Copyright Epic Games, Inc. All Rights Reserved.

#include "Transport/MockNetTransport.h"

#include "Codec/NetPacketCodec.h"

/**
 * @brief 建立内存虚拟回环连接（受 SetAcceptConnections 开关控制）
 * @param Endpoint 非空虚拟端点标识
 * @return 连接成功返回成功态，端点为空或拒绝连接时返回对应错误
 */
TGameStatusResult<void> FMockNetTransport::Connect(const FString& Endpoint) {
    if (Endpoint.IsEmpty()) {
        TransportErrorEvent.Broadcast(TEXT("MockNetTransport rejected empty endpoint."));
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2001, TEXT("MockNetTransport rejected empty endpoint"))
        );
    }
    if (CurrentState == ENetConnectionState::Connected || CurrentState == ENetConnectionState::Connecting) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2002, TEXT("Connection already established or in progress"))
        );
    }

    TransitionState(ENetConnectionState::Connecting);
    if (!bAcceptConnections) {
        TransitionState(ENetConnectionState::Disconnected);
        TransportErrorEvent.Broadcast(FString::Printf(TEXT("MockNetTransport refused connection to '%s'."), *Endpoint));
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2001, FString::Printf(TEXT("MockNetTransport refused connection to '%s'"), *Endpoint))
        );
    }

    ConnectedEndpoint = Endpoint;
    TransitionState(ENetConnectionState::Connected);
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 断开内存虚拟连接并清空待派发回环队列
 * @param ReasonCode 断开原因码
 * @param ReasonDetail 断开原因说明
 */
void FMockNetTransport::Disconnect([[maybe_unused]] int32 ReasonCode, [[maybe_unused]] const FString& ReasonDetail) {
    if (CurrentState == ENetConnectionState::Disconnected) {
        return;
    }

    TransitionState(ENetConnectionState::Closing);
    PendingInboundQueue.Empty();
    ConnectedEndpoint.Reset();
    TransitionState(ENetConnectionState::Disconnected);
}

/**
 * @brief 向内存虚拟路由投递请求包；命中模拟处理器或开启回声时入队响应，在下一轮 Tick 主线程派发
 * @param Header 报文头（自动对齐 PayloadLength）
 * @param Payload 负载字节切片
 * @return 已连接且报文头合法时返回成功态，否则返回对应错误
 */
TGameStatusResult<void> FMockNetTransport::Send(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload) {
    if (CurrentState != ENetConnectionState::Connected) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2002, TEXT("Transport is not connected"))
        );
    }

    FNetPacketHeader OutboundHeader = Header;
    OutboundHeader.PayloadLength = static_cast<uint32>(Payload.Num());
    if (!FNetPacketCodec::IsValidHeader(OutboundHeader)) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2003, TEXT("Invalid packet header"))
        );
    }

    ++SentPacketCount;

    if (const FRegisteredMockRoute* Route = MockRoutes.Find(OutboundHeader.MessageId)) {
        if (Route->Handler) {
            TOptional<TArray<uint8>> ResponsePayload = Route->Handler(OutboundHeader, Payload);
            if (ResponsePayload.IsSet()) {
                FNetPacket InboundPacket;
                InboundPacket.Header.Magic = FNetPacketHeader::PACKET_MAGIC;
                InboundPacket.Header.HeaderSize = FNetPacketHeader::WIRE_HEADER_BYTES;
                InboundPacket.Header.MessageId = Route->ResponseMessageId;
                InboundPacket.Header.SequenceId = OutboundHeader.SequenceId;
                InboundPacket.Header.PayloadLength = static_cast<uint32>(ResponsePayload->Num());
                InboundPacket.Payload = MoveTemp(*ResponsePayload);
                PendingInboundQueue.Enqueue(MoveTemp(InboundPacket));
            }
        }
        return TGameStatusResult<void>::Success();
    }

    if (bEchoUnregistered) {
        FNetPacket EchoPacket;
        EchoPacket.Header = OutboundHeader;
        EchoPacket.Payload.Append(Payload.GetData(), Payload.Num());
        PendingInboundQueue.Enqueue(MoveTemp(EchoPacket));
    }
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 消费内存待派发响应队列并在当前主线程触发 OnPacketReceived 广播
 * @param DeltaTimeSeconds 帧间隔秒数
 */
void FMockNetTransport::Tick([[maybe_unused]] float DeltaTimeSeconds) {
    if (CurrentState != ENetConnectionState::Connected) {
        return;
    }

    FNetPacket InboundPacket;
    while (CurrentState == ENetConnectionState::Connected && PendingInboundQueue.Dequeue(InboundPacket)) {
        PacketReceivedEvent.Broadcast(InboundPacket.Header, InboundPacket.Payload);
    }
}

/**
 * @brief 注册针对特定请求协议 ID 的单机本地模拟处理器（自动继承请求 SequenceId）
 * @param RequestMessageId 客户端发送的请求协议 ID
 * @param ResponseMessageId 回传响应的协议 ID
 * @param Handler 请求处理闭包
 */
void FMockNetTransport::RegisterMockHandler(
    uint32 RequestMessageId,
    uint32 ResponseMessageId,
    FMockRequestHandler Handler
) {
    FRegisteredMockRoute Route;
    Route.ResponseMessageId = ResponseMessageId;
    Route.Handler = MoveTemp(Handler);
    MockRoutes.Add(RequestMessageId, MoveTemp(Route));
}

/**
 * @brief 移除指定请求协议 ID 的本地模拟处理器
 * @param RequestMessageId 待移除的请求协议 ID
 */
void FMockNetTransport::UnregisterMockHandler(uint32 RequestMessageId) {
    MockRoutes.Remove(RequestMessageId);
}

/**
 * @brief 清空所有已注册的本地模拟处理器
 */
void FMockNetTransport::ClearMockHandlers() {
    MockRoutes.Empty();
}

/**
 * @brief 主动模拟服务端向客户端推送一条下行消息（入队并在 Tick 时派发）
 * @param MessageId 下行推送协议 ID
 * @param Payload 推送负载字节切片
 * @param SequenceId 序列号
 * @return 当前处于已连接状态且入队成功时返回 true
 */
bool FMockNetTransport::SimulateServerPush(uint32 MessageId, TConstArrayView<uint8> Payload, uint32 SequenceId) {
    if (CurrentState != ENetConnectionState::Connected) {
        return false;
    }

    FNetPacket PushPacket;
    PushPacket.Header.Magic = FNetPacketHeader::PACKET_MAGIC;
    PushPacket.Header.HeaderSize = FNetPacketHeader::WIRE_HEADER_BYTES;
    PushPacket.Header.PayloadLength = static_cast<uint32>(Payload.Num());
    PushPacket.Header.MessageId = MessageId;
    PushPacket.Header.SequenceId = SequenceId;
    if (!FNetPacketCodec::IsValidHeader(PushPacket.Header)) {
        return false;
    }

    PushPacket.Payload.Append(Payload.GetData(), Payload.Num());
    PendingInboundQueue.Enqueue(MoveTemp(PushPacket));
    return true;
}

/**
 * @brief 模拟远端服务器主动踢下线或网络闪断
 * @param ErrorDetail 断线错误详情描述
 */
void FMockNetTransport::SimulateRemoteDisconnect(const FString& ErrorDetail) {
    if (CurrentState == ENetConnectionState::Disconnected) {
        return;
    }

    PendingInboundQueue.Empty();
    ConnectedEndpoint.Reset();
    TransitionState(ENetConnectionState::Disconnected);
    TransportErrorEvent.Broadcast(ErrorDetail);
}

/**
 * @brief 变更连接状态并在状态实际发生变化时触发广播
 * @param NewState 目标新状态
 */
void FMockNetTransport::TransitionState(ENetConnectionState NewState) {
    if (CurrentState == NewState) {
        return;
    }
    CurrentState = NewState;
    StateChangedEvent.Broadcast(CurrentState);
}

