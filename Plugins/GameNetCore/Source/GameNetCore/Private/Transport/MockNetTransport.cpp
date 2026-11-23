// Copyright Epic Games, Inc. All Rights Reserved.

#include "Transport/MockNetTransport.h"

#include "Codec/NetPacketCodec.h"
#include "Logging/GameLogCategories.h"
#include "Types/NetErrorCodes.h"

/**
 * @brief 建立内存虚拟回环连接（受 SetAcceptConnections 开关控制）
 * @param Endpoint 非空虚拟端点标识
 * @return 连接成功返回成功态，端点为空或拒绝连接时返回对应错误
 */
TGameStatusResult<void> FMockNetTransport::Connect(const FString& Endpoint) {
    if (Endpoint.IsEmpty()) {
        GAME_LOG(Warning, TEXT("[MockTransport] 拒绝空端点连接请求。"));
        TransportErrorEvent.Broadcast(TEXT("MockNetTransport rejected empty endpoint."));
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::InvalidEndpoint,
            TEXT("MockNetTransport rejected empty endpoint")
        );
    }
    if (CurrentState == ENetConnectionState::Connected || CurrentState == ENetConnectionState::Connecting) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::AlreadyConnected,
            TEXT("Connection already established or in progress")
        );
    }

    TransitionState(ENetConnectionState::Connecting);
    if (!bAcceptConnections) {
        TransitionState(ENetConnectionState::Disconnected);
        const FString ErrorMsg = FString::Printf(TEXT("MockNetTransport refused connection to '%s'"), *Endpoint);
        GAME_LOG(Warning, TEXT("[MockTransport] 模拟端点拒绝连接: %s"), *Endpoint);
        TransportErrorEvent.Broadcast(ErrorMsg);
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::ConnectionRefused,
            ErrorMsg
        );
    }

    ConnectedEndpoint = Endpoint;
    TransitionState(ENetConnectionState::Connected);
    GAME_LOG(Verbose, TEXT("[MockTransport] 已建立内存回环连接: %s"), *Endpoint);
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
            Game::ModuleId::Net,
            Game::NetErrorCodes::NotConnected,
            TEXT("Transport is not connected")
        );
    }

    FNetPacketHeader OutboundHeader = Header;
    OutboundHeader.PayloadLength = static_cast<uint32>(Payload.Num());
    if (!FNetPacketCodec::IsValidHeader(OutboundHeader)) {
        GAME_LOG_THROTTLE(
            1.0,
            Warning,
            TEXT("[MockTransport] 发送报文头校验失败: Magic=0x%04X, PayloadLen=%u"),
            OutboundHeader.Magic,
            OutboundHeader.PayloadLength
        );
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::InvalidPacketHeader,
            TEXT("Invalid packet header")
        );
    }

    ++SentPacketCount;

    if (TryDispatchMockRoute(OutboundHeader, Payload)) {
        return TGameStatusResult<void>::Success();
    }

    if (bEchoUnregistered) {
        FNetPacket EchoPacket(OutboundHeader, Payload);
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

    FNetPacketHeader PushHeader;
    PushHeader.PayloadLength = static_cast<uint32>(Payload.Num());
    PushHeader.MessageId = MessageId;
    PushHeader.SequenceId = SequenceId;
    if (!FNetPacketCodec::IsValidHeader(PushHeader)) {
        return false;
    }

    FNetPacket PushPacket(PushHeader, Payload);
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
 * @brief 尝试将上行报文派发至已注册的模拟处理器（先快照路由再回调以保障重入安全）
 * @param OutboundHeader 归一化后的上行报文头
 * @param Payload 负载二进制切片
 * @return 命中已注册路由返回 true，未命中返回 false
 */
bool FMockNetTransport::TryDispatchMockRoute(const FNetPacketHeader& OutboundHeader, TConstArrayView<uint8> Payload) {
    const FRegisteredMockRoute* Route = MockRoutes.Find(OutboundHeader.MessageId);
    if (Route == nullptr) {
        return false;
    }

    const uint32 ResponseMessageId = Route->ResponseMessageId;
    const FMockRequestHandler HandlerCopy = Route->Handler;
    if (!HandlerCopy) {
        return true;
    }

    TOptional<TArray<uint8>> ResponsePayload = HandlerCopy(OutboundHeader, Payload);
    if (!ResponsePayload.IsSet() || CurrentState != ENetConnectionState::Connected) {
        return true;
    }

    FNetPacket InboundPacket;
    InboundPacket.Header.MessageId = ResponseMessageId;
    InboundPacket.Header.SequenceId = OutboundHeader.SequenceId;
    InboundPacket.Header.PayloadLength = static_cast<uint32>(ResponsePayload->Num());
    InboundPacket.Payload = MoveTemp(*ResponsePayload);
    PendingInboundQueue.Enqueue(MoveTemp(InboundPacket));
    return true;
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
