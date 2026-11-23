// Copyright Epic Games, Inc. All Rights Reserved.

#include "Transport/WebSocketNetTransport.h"

#include "Codec/NetPacketCodec.h"
#include "IWebSocket.h"
#include "Logging/GameLogCategories.h"
#include "Modules/ModuleManager.h"
#include "Types/NetErrorCodes.h"
#include "WebSocketsModule.h"

namespace {

/**
 * @brief 通过引擎原生 WebSockets 模块创建底层 IWebSocket 实例
 * @param Endpoint WebSocket 目标地址
 * @param SubProtocol 协商子协议名
 * @param UpgradeHeaders 握手扩展头映射表
 * @return 创建出的 IWebSocket 共享指针
 */
TSharedPtr<IWebSocket> CreateEngineWebSocket(
    const FString& Endpoint,
    const FString& SubProtocol,
    const TMap<FString, FString>& UpgradeHeaders
) {
#if WITH_WEBSOCKETS
    FWebSocketsModule& WebSocketsModule = FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
    return WebSocketsModule.CreateWebSocket(Endpoint, SubProtocol, UpgradeHeaders);
#else
    return nullptr;
#endif
}

}  // namespace

/**
 * @brief 构造 WebSocket 传输驱动
 * @param InMaxPayloadBytes 单帧允许接收的最大业务负载字节数
 * @param InCustomFactory 可选底层 WebSocket 实例工厂
 */
FWebSocketNetTransport::FWebSocketNetTransport(uint32 InMaxPayloadBytes, FWebSocketFactory InCustomFactory)
    : SocketFactory(InCustomFactory ? MoveTemp(InCustomFactory) : FWebSocketFactory(&CreateEngineWebSocket))
    , EventChannel(MakeShared<FInboundEventChannel, ESPMode::ThreadSafe>())
    , FragmentAssembler(InMaxPayloadBytes)
    , ReceiveRingBuffer(FNetPacketRingBuffer::DEFAULT_INITIAL_CAPACITY_BYTES, InMaxPayloadBytes) {
}

/**
 * @brief 析构时安全解绑底层回调并关闭物理连接
 */
FWebSocketNetTransport::~FWebSocketNetTransport() {
    ++ActiveGeneration;
    ReleaseSocket(1000, TEXT("TransportDestroyed"), true);
    EventChannel->Reset();
}

/**
 * @brief 发起向指定 ws:// 或 wss:// 端点的异步握手连接
 * @param Endpoint WebSocket 目标 URL
 * @return 成功创建底层实例并发起连接返回成功态，否则返回对应错误
 */
TGameStatusResult<void> FWebSocketNetTransport::Connect(const FString& Endpoint) {
    if (Endpoint.IsEmpty()) {
        GAME_LOG(Warning, TEXT("[WebSocket] 拒绝空端点地址的连接请求。"));
        TransportErrorEvent.Broadcast(TEXT("WebSocketNetTransport rejected empty endpoint."));
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::InvalidEndpoint,
            TEXT("WebSocketNetTransport rejected empty endpoint")
        );
    }
    if (CurrentState == ENetConnectionState::Connected || CurrentState == ENetConnectionState::Connecting) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::AlreadyConnected,
            TEXT("Connection already established or connecting")
        );
    }

    ++ActiveGeneration;
    FragmentAssembler.Reset();
    ReceiveRingBuffer.Reset();
    EventChannel = MakeShared<FInboundEventChannel, ESPMode::ThreadSafe>();

    const TSharedPtr<IWebSocket> CreatedSocket = SocketFactory(Endpoint, SubProtocol, UpgradeHeaders);
    if (!CreatedSocket.IsValid()) {
        GAME_LOG(Warning, TEXT("[WebSocket] 创建底层 IWebSocket 实例失败: %s"), *Endpoint);
        TransportErrorEvent.Broadcast(TEXT("Failed to instantiate underlying IWebSocket instance."));
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::TransportUnavailable,
            TEXT("Failed to instantiate underlying IWebSocket instance")
        );
    }

    ActiveSocket = CreatedSocket;
    BindSocketCallbacks(CreatedSocket.ToSharedRef(), ActiveGeneration);
    TransitionState(ENetConnectionState::Connecting);
    GAME_LOG(Log, TEXT("[WebSocket] 发起异步握手连接: %s (Gen=%llu)"), *Endpoint, ActiveGeneration);
    CreatedSocket->Connect();
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 主动关闭当前 WebSocket 连接并使旧代际回调失效
 * @param ReasonCode 标准 WebSocket 关闭状态码
 * @param ReasonDetail 关闭原因描述
 */
void FWebSocketNetTransport::Disconnect(int32 ReasonCode, const FString& ReasonDetail) {
    if (CurrentState == ENetConnectionState::Disconnected) {
        return;
    }

    GAME_LOG(Log, TEXT("[WebSocket] 主动断开连接: Code=%d, Reason=%s"), ReasonCode, *ReasonDetail);
    ++ActiveGeneration;
    TransitionState(ENetConnectionState::Closing);
    ReleaseSocket(ReasonCode, ReasonDetail, true);
    FragmentAssembler.Reset();
    ReceiveRingBuffer.Reset();
    EventChannel->Reset();
    TransitionState(ENetConnectionState::Disconnected);
}

/**
 * @brief 将报文头与负载编码为 16 字节线格式二进制帧并通过 WebSocket Binary 通道发送
 * @param Header 报文头
 * @param Payload 负载二进制切片
 * @return 已连接且帧合法时成功写入返回成功态，否则返回对应错误
 */
TGameStatusResult<void> FWebSocketNetTransport::Send(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload) {
    if (CurrentState != ENetConnectionState::Connected || !ActiveSocket.IsValid()) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::NotConnected,
            TEXT("WebSocket is not connected")
        );
    }

    FNetPacketHeader OutboundHeader = Header;
    OutboundHeader.PayloadLength = static_cast<uint32>(Payload.Num());
    if (!FNetPacketCodec::IsValidHeader(OutboundHeader, ReceiveRingBuffer.GetMaxPayloadBytes())) {
        GAME_LOG_THROTTLE(
            1.0,
            Warning,
            TEXT("[WebSocket] 拦截非法外发报文头: Magic=0x%04X, PayloadLen=%u, Max=%u"),
            OutboundHeader.Magic,
            OutboundHeader.PayloadLength,
            ReceiveRingBuffer.GetMaxPayloadBytes()
        );
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::InvalidPacketHeader,
            TEXT("Invalid packet header or payload exceeds max allowed limit")
        );
    }

    FNetPacketCodec::EncodeFrameTo(OutboundHeader, Payload, OutboundScratchBuffer);
    ActiveSocket->Send(OutboundScratchBuffer.GetData(), static_cast<SIZE_T>(OutboundScratchBuffer.Num()), true);
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 在游戏主线程消费有界无锁环形队列中的后台 Socket 事件并触发上层委托
 * @param DeltaTimeSeconds 帧间隔秒数
 */
void FWebSocketNetTransport::Tick([[maybe_unused]] float DeltaTimeSeconds) {
    if (EventChannel->bOverflowed.exchange(false, std::memory_order_acq_rel)) {
        TriggerProtocolCircuitBreaker(TEXT("WebSocket inbound event ring buffer overflowed."));
        return;
    }

    FInboundSocketEvent Event;
    while (EventChannel->Queue.Dequeue(Event)) {
        if (Event.Generation != ActiveGeneration) {
            continue;
        }
        HandleInboundEvent(Event);
    }
}

/**
 * @brief 挂载底层 IWebSocket 事件回调至有界无锁跨线程通道（按代际隔离并在 IO 线程前置拦截超大单分片）
 * @param TargetSocket 待绑定的底层 Socket 引用
 * @param BoundGeneration 当前连接会话代际号
 */
void FWebSocketNetTransport::BindSocketCallbacks(
    const TSharedRef<IWebSocket>& TargetSocket,
    uint64 BoundGeneration
) {
    const TSharedRef<FInboundEventChannel, ESPMode::ThreadSafe> ChannelRef = EventChannel;
    const uint64 MaxFrameBytesLimit = static_cast<uint64>(FragmentAssembler.GetMaxFrameBytes());

    TargetSocket->OnConnected().AddLambda([ChannelRef, BoundGeneration]() {
        FInboundSocketEvent Event;
        Event.Generation = BoundGeneration;
        Event.Kind = EInboundEventKind::Connected;
        ChannelRef->PushOrMarkOverflow(MoveTemp(Event));
    });

    TargetSocket->OnConnectionError().AddLambda([ChannelRef, BoundGeneration](const FString& ErrorText) {
        FInboundSocketEvent Event;
        Event.Generation = BoundGeneration;
        Event.Kind = EInboundEventKind::ConnectionError;
        Event.TextDetail = ErrorText;
        ChannelRef->PushOrMarkOverflow(MoveTemp(Event));
    });

    TargetSocket->OnClosed().AddLambda([ChannelRef, BoundGeneration](int32 StatusCode, const FString& Reason, bool bWasClean) {
        FInboundSocketEvent Event;
        Event.Generation = BoundGeneration;
        Event.Kind = EInboundEventKind::Closed;
        Event.StatusCode = StatusCode;
        Event.TextDetail = Reason;
        Event.bWasClean = bWasClean;
        ChannelRef->PushOrMarkOverflow(MoveTemp(Event));
    });

    TargetSocket->OnBinaryMessage().AddLambda(
        [ChannelRef, BoundGeneration, MaxFrameBytesLimit](const void* Data, SIZE_T Size, bool bIsLastFragment) {
            FInboundSocketEvent Event;
            Event.Generation = BoundGeneration;
            if (static_cast<uint64>(Size) > MaxFrameBytesLimit) {
                Event.Kind = EInboundEventKind::ProtocolViolation;
                Event.TextDetail = FString::Printf(
                    TEXT("WebSocket binary fragment size (%llu) exceeded max frame limit (%llu)."),
                    static_cast<uint64>(Size),
                    MaxFrameBytesLimit
                );
                ChannelRef->PushOrMarkOverflow(MoveTemp(Event));
                return;
            }

            Event.Kind = EInboundEventKind::BinaryFragment;
            Event.bIsLastFragment = bIsLastFragment;
            if (Data != nullptr && Size > 0) {
                Event.BinaryData.SetNumUninitialized(static_cast<int32>(Size));
                FMemory::Memcpy(Event.BinaryData.GetData(), Data, Size);
            }
            ChannelRef->PushOrMarkOverflow(MoveTemp(Event));
        }
    );
}

/**
 * @brief 解绑并安全释放当前持有的底层 IWebSocket 实例
 * @param CloseCode 关闭码
 * @param CloseReason 关闭描述
 * @param bShouldInvokeClose 是否需要向底层实例发起 Close 调用
 */
void FWebSocketNetTransport::ReleaseSocket(int32 CloseCode, const FString& CloseReason, bool bShouldInvokeClose) {
    if (!ActiveSocket.IsValid()) {
        return;
    }

    TSharedPtr<IWebSocket> DyingSocket = MoveTemp(ActiveSocket);
    ActiveSocket.Reset();

    DyingSocket->OnConnected().Clear();
    DyingSocket->OnConnectionError().Clear();
    DyingSocket->OnClosed().Clear();
    DyingSocket->OnMessage().Clear();
    DyingSocket->OnBinaryMessage().Clear();
    DyingSocket->OnRawMessage().Clear();
    DyingSocket->OnMessageSent().Clear();

    if (bShouldInvokeClose) {
        DyingSocket->Close(CloseCode, CloseReason);
    }
}

/**
 * @brief 在主线程分发单条后台 Socket 事件
 * @param Event 已通过代际校验的后台事件
 */
void FWebSocketNetTransport::HandleInboundEvent(const FInboundSocketEvent& Event) {
    switch (Event.Kind) {
        case EInboundEventKind::Connected:
            GAME_LOG(Log, TEXT("[WebSocket] 握手建立成功 (Gen=%llu)。"), ActiveGeneration);
            TransitionState(ENetConnectionState::Connected);
            break;

        case EInboundEventKind::ConnectionError:
            HandleSocketDisconnected(1006, Event.TextDetail, true, Event.TextDetail);
            break;

        case EInboundEventKind::Closed: {
            const FString ErrorMsg = Event.bWasClean
                ? FString()
                : FString::Printf(TEXT("WebSocket closed uncleanly (code=%d): %s"), Event.StatusCode, *Event.TextDetail);
            HandleSocketDisconnected(Event.StatusCode, Event.TextDetail, !Event.bWasClean, ErrorMsg);
            break;
        }

        case EInboundEventKind::BinaryFragment:
            ProcessBinaryFragment(Event.BinaryData, Event.bIsLastFragment);
            break;

        case EInboundEventKind::ProtocolViolation:
            TriggerProtocolCircuitBreaker(Event.TextDetail);
            break;
    }
}

/**
 * @brief 统一处理连接错误与远端关闭导致的断线清理与状态迁移
 * @param CloseCode 关闭状态码
 * @param CloseDetail 关闭原因描述
 * @param bShouldBroadcastError 是否需要向 OnTransportError 广播异常
 * @param ErrorMessage 广播错误文案
 */
void FWebSocketNetTransport::HandleSocketDisconnected(
    int32 CloseCode,
    const FString& CloseDetail,
    bool bShouldBroadcastError,
    const FString& ErrorMessage
) {
    if (bShouldBroadcastError) {
        GAME_LOG(Warning, TEXT("[WebSocket] 连接异常断开: Code=%d, Detail=%s"), CloseCode, *ErrorMessage);
    } else {
        GAME_LOG(Log, TEXT("[WebSocket] 连接正常关闭: Code=%d, Detail=%s"), CloseCode, *CloseDetail);
    }

    ++ActiveGeneration;
    ReleaseSocket(CloseCode, CloseDetail, false);
    FragmentAssembler.Reset();
    ReceiveRingBuffer.Reset();
    TransitionState(ENetConnectionState::Disconnected);
    if (bShouldBroadcastError) {
        TransportErrorEvent.Broadcast(ErrorMessage);
    }
}

/**
 * @brief 处理单条后台队列出队的二进制分片并在完整消息收齐后直通解帧或送入环形缓冲
 * @param FragmentBytes 分片数据
 * @param bIsLastFragment 是否为当前 WebSocket 消息的最后一个分片
 */
void FWebSocketNetTransport::ProcessBinaryFragment(TConstArrayView<uint8> FragmentBytes, bool bIsLastFragment) {
    TConstArrayView<uint8> AssembledView;
    const EWebSocketFragmentStatus Status =
        FragmentAssembler.AppendFragment(FragmentBytes, bIsLastFragment, AssembledView);
    if (Status == EWebSocketFragmentStatus::FrameExceedsLimit) {
        TriggerProtocolCircuitBreaker(TEXT("WebSocket binary fragment exceeded maximum allowed frame size."));
        return;
    }
    if (Status != EWebSocketFragmentStatus::MessageReady || AssembledView.IsEmpty()) {
        return;
    }

    if (ReceiveRingBuffer.GetReadableBytes() == 0) {
        DispatchDirectOrBuffer(AssembledView);
        return;
    }

    if (!ReceiveRingBuffer.Append(AssembledView)) {
        TriggerProtocolCircuitBreaker(TEXT("Receive ring buffer exceeded maximum safe capacity."));
        return;
    }
    DrainDecodedFrames();
}

/**
 * @brief 环形缓冲为空时直接从连续消息视图零拷贝解帧派发，仅在存在半包尾段时压入环形缓冲
 * @param MessageBytes 已拼装完整的 WebSocket 二进制消息视图
 */
void FWebSocketNetTransport::DispatchDirectOrBuffer(TConstArrayView<uint8> MessageBytes) {
    const uint64 DispatchGeneration = ActiveGeneration;
    const uint32 MaxPayloadBytes = ReceiveRingBuffer.GetMaxPayloadBytes();
    int32 Offset = 0;
    const int32 TotalBytes = MessageBytes.Num();

    while (CurrentState == ENetConnectionState::Connected && ActiveGeneration == DispatchGeneration && Offset < TotalBytes) {
        const TConstArrayView<uint8> Remaining(MessageBytes.GetData() + Offset, TotalBytes - Offset);
        FNetPacketHeader Header;
        if (!FNetPacketCodec::TryDecodeHeader(Remaining, Header)) {
            if (!ReceiveRingBuffer.Append(Remaining)) {
                TriggerProtocolCircuitBreaker(TEXT("Receive ring buffer exceeded maximum safe capacity."));
            }
            return;
        }

        const ENetFrameParseStatus HeaderStatus = FNetPacketCodec::ValidateHeader(Header, MaxPayloadBytes);
        if (HeaderStatus != ENetFrameParseStatus::CompleteFrame) {
            TriggerProtocolCircuitBreaker(
                FString::Printf(TEXT("Malformed wire frame rejected (status=%d)."), static_cast<int32>(HeaderStatus))
            );
            return;
        }

        const int64 FrameTotalBytes =
            static_cast<int64>(FNetPacketHeader::WIRE_HEADER_BYTES) + static_cast<int64>(Header.PayloadLength);
        if (static_cast<int64>(Remaining.Num()) < FrameTotalBytes) {
            if (!ReceiveRingBuffer.Append(Remaining)) {
                TriggerProtocolCircuitBreaker(TEXT("Receive ring buffer exceeded maximum safe capacity."));
            }
            return;
        }

        const TConstArrayView<uint8> PayloadView(
            Remaining.GetData() + FNetPacketHeader::WIRE_HEADER_BYTES,
            static_cast<int32>(Header.PayloadLength)
        );
        Offset += static_cast<int32>(FrameTotalBytes);
        PacketReceivedEvent.Broadcast(Header, PayloadView);
    }
}

/**
 * @brief 从环形解析器连续消费所有完整帧并广播；遇协议违例立即熔断断连
 */
void FWebSocketNetTransport::DrainDecodedFrames() {
    const uint64 DrainGeneration = ActiveGeneration;
    while (CurrentState == ENetConnectionState::Connected && ActiveGeneration == DrainGeneration) {
        const ENetFrameParseStatus Status = ReceiveRingBuffer.TryConsumePacket(
            [this](const FNetPacketHeader& Header, TConstArrayView<uint8> Payload) {
                PacketReceivedEvent.Broadcast(Header, Payload);
            }
        );
        if (Status == ENetFrameParseStatus::CompleteFrame) {
            continue;
        }
        if (Status != ENetFrameParseStatus::NeedMoreData) {
            TriggerProtocolCircuitBreaker(
                FString::Printf(TEXT("Malformed wire frame rejected (status=%d)."), static_cast<int32>(Status))
            );
        }
        return;
    }
}

/**
 * @brief 因对端发来畸形头、超限包或事件队列溢出触发协议级熔断保护
 * @param ErrorReason 熔断原因说明
 */
void FWebSocketNetTransport::TriggerProtocolCircuitBreaker(const FString& ErrorReason) {
    GAME_LOG(Warning, TEXT("[WebSocket] 触发协议级熔断保护并关闭连接: %s"), *ErrorReason);
    ++ActiveGeneration;
    ReleaseSocket(1002, ErrorReason, true);
    FragmentAssembler.Reset();
    ReceiveRingBuffer.Reset();
    EventChannel->Reset();
    TransitionState(ENetConnectionState::Disconnected);
    TransportErrorEvent.Broadcast(ErrorReason);
}

/**
 * @brief 更新内部连接状态并在状态变化时触发广播
 * @param NewState 目标新状态
 */
void FWebSocketNetTransport::TransitionState(ENetConnectionState NewState) {
    if (CurrentState == NewState) {
        return;
    }
    CurrentState = NewState;
    StateChangedEvent.Broadcast(CurrentState);
}
