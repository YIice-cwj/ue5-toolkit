// Copyright Epic Games, Inc. All Rights Reserved.

#include "Transport/WebSocketNetTransport.h"

#include "Codec/NetPacketCodec.h"
#include "IWebSocket.h"
#include "Modules/ModuleManager.h"
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

}

/**
 * @brief 构造 WebSocket 传输驱动
 * @param InMaxPayloadBytes 单帧允许接收的最大业务负载字节数
 * @param InCustomFactory 可选底层 WebSocket 实例工厂
 */
FWebSocketNetTransport::FWebSocketNetTransport(uint32 InMaxPayloadBytes, FWebSocketFactory InCustomFactory)
    : SocketFactory(InCustomFactory ? MoveTemp(InCustomFactory) : FWebSocketFactory(&CreateEngineWebSocket))
    , SharedEventQueue(MakeShared<FThreadSafeEventQueue, ESPMode::ThreadSafe>())
    , FragmentAssembler(InMaxPayloadBytes)
    , ReceiveRingBuffer(FNetPacketRingBuffer::DEFAULT_INITIAL_CAPACITY_BYTES, InMaxPayloadBytes) {
}

/**
 * @brief 析构时安全解绑底层回调并关闭物理连接
 */
FWebSocketNetTransport::~FWebSocketNetTransport() {
    ++ActiveGeneration;
    ReleaseSocket(1000, TEXT("TransportDestroyed"), true);
}

/**
 * @brief 发起向指定 ws:// 或 wss:// 端点的异步握手连接
 * @param Endpoint WebSocket 目标 URL
 * @return 成功创建底层实例并发起连接返回成功态，否则返回对应错误
 */
TGameStatusResult<void> FWebSocketNetTransport::Connect(const FString& Endpoint) {
    if (Endpoint.IsEmpty()) {
        TransportErrorEvent.Broadcast(TEXT("WebSocketNetTransport rejected empty endpoint."));
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2001, TEXT("WebSocketNetTransport rejected empty endpoint"))
        );
    }
    if (CurrentState == ENetConnectionState::Connected || CurrentState == ENetConnectionState::Connecting) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2002, TEXT("Connection already established or connecting"))
        );
    }

    ++ActiveGeneration;
    FragmentAssembler.Reset();
    ReceiveRingBuffer.Reset();
    SharedEventQueue->Empty();

    const TSharedPtr<IWebSocket> CreatedSocket = SocketFactory(Endpoint, SubProtocol, UpgradeHeaders);
    if (!CreatedSocket.IsValid()) {
        TransportErrorEvent.Broadcast(TEXT("Failed to instantiate underlying IWebSocket instance."));
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2001, TEXT("Failed to instantiate underlying IWebSocket instance"))
        );
    }

    ActiveSocket = CreatedSocket;
    BindSocketCallbacks(CreatedSocket.ToSharedRef(), ActiveGeneration);
    TransitionState(ENetConnectionState::Connecting);
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

    ++ActiveGeneration;
    TransitionState(ENetConnectionState::Closing);
    ReleaseSocket(ReasonCode, ReasonDetail, true);
    FragmentAssembler.Reset();
    ReceiveRingBuffer.Reset();
    SharedEventQueue->Empty();
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
            FGameError::Make(Game::ModuleId::Net, 2002, TEXT("WebSocket is not connected"))
        );
    }

    FNetPacketHeader OutboundHeader = Header;
    OutboundHeader.PayloadLength = static_cast<uint32>(Payload.Num());
    if (!FNetPacketCodec::IsValidHeader(OutboundHeader, ReceiveRingBuffer.GetMaxPayloadBytes())) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2003, TEXT("Invalid packet header or payload exceeds max allowed limit"))
        );
    }

    const TArray<uint8> WireFrame = FNetPacketCodec::EncodeFrame(OutboundHeader, Payload);
    ActiveSocket->Send(WireFrame.GetData(), static_cast<SIZE_T>(WireFrame.Num()), true);
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 在游戏主线程消费无锁队列中的后台 Socket 事件并触发上层委托
 * @param DeltaTimeSeconds 帧间隔秒数
 */
void FWebSocketNetTransport::Tick([[maybe_unused]] float DeltaTimeSeconds) {
    FInboundSocketEvent Event;
    while (SharedEventQueue->Dequeue(Event)) {
        if (Event.Generation != ActiveGeneration) {
            continue;
        }

        switch (Event.Kind) {
            case EInboundEventKind::Connected:
                TransitionState(ENetConnectionState::Connected);
                break;

            case EInboundEventKind::ConnectionError:
                ++ActiveGeneration;
                ReleaseSocket(1006, Event.TextDetail, false);
                FragmentAssembler.Reset();
                ReceiveRingBuffer.Reset();
                TransitionState(ENetConnectionState::Disconnected);
                TransportErrorEvent.Broadcast(Event.TextDetail);
                break;

            case EInboundEventKind::Closed:
                ++ActiveGeneration;
                ReleaseSocket(Event.StatusCode, Event.TextDetail, false);
                FragmentAssembler.Reset();
                ReceiveRingBuffer.Reset();
                TransitionState(ENetConnectionState::Disconnected);
                if (!Event.bWasClean) {
                    TransportErrorEvent.Broadcast(
                        FString::Printf(TEXT("WebSocket closed uncleanly (code=%d): %s"), Event.StatusCode, *Event.TextDetail)
                    );
                }
                break;

            case EInboundEventKind::BinaryFragment:
                ProcessBinaryFragment(Event.BinaryData, Event.bIsLastFragment);
                break;
        }
    }
}

/**
 * @brief 挂载底层 IWebSocket 事件回调至无锁跨线程队列（按代际隔离）
 * @param TargetSocket 待绑定的底层 Socket 引用
 * @param BoundGeneration 当前连接会话代际号
 */
void FWebSocketNetTransport::BindSocketCallbacks(
    const TSharedRef<IWebSocket>& TargetSocket,
    uint64 BoundGeneration
) {
    const TSharedRef<FThreadSafeEventQueue, ESPMode::ThreadSafe> QueueRef = SharedEventQueue;

    TargetSocket->OnConnected().AddLambda([QueueRef, BoundGeneration]() {
        FInboundSocketEvent Event;
        Event.Generation = BoundGeneration;
        Event.Kind = EInboundEventKind::Connected;
        QueueRef->Enqueue(MoveTemp(Event));
    });

    TargetSocket->OnConnectionError().AddLambda([QueueRef, BoundGeneration](const FString& ErrorText) {
        FInboundSocketEvent Event;
        Event.Generation = BoundGeneration;
        Event.Kind = EInboundEventKind::ConnectionError;
        Event.TextDetail = ErrorText;
        QueueRef->Enqueue(MoveTemp(Event));
    });

    TargetSocket->OnClosed().AddLambda([QueueRef, BoundGeneration](int32 StatusCode, const FString& Reason, bool bWasClean) {
        FInboundSocketEvent Event;
        Event.Generation = BoundGeneration;
        Event.Kind = EInboundEventKind::Closed;
        Event.StatusCode = StatusCode;
        Event.TextDetail = Reason;
        Event.bWasClean = bWasClean;
        QueueRef->Enqueue(MoveTemp(Event));
    });

    TargetSocket->OnBinaryMessage().AddLambda(
        [QueueRef, BoundGeneration](const void* Data, SIZE_T Size, bool bIsLastFragment) {
            FInboundSocketEvent Event;
            Event.Generation = BoundGeneration;
            Event.Kind = EInboundEventKind::BinaryFragment;
            Event.bIsLastFragment = bIsLastFragment;
            if (Data != nullptr && Size > 0) {
                Event.BinaryData.Append(static_cast<const uint8*>(Data), static_cast<int32>(Size));
            }
            QueueRef->Enqueue(MoveTemp(Event));
        }
    );
}

/**
 * @brief 解绑并安全释放当前持有的底层 IWebSocket 实例
 * @param CloseCode 关闭码
 * @param CloseReason 关闭描述
 * @param bShouldInvokeClose 是否需要向未关闭的底层实例发起 Close 调用
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

    if (bShouldInvokeClose && DyingSocket->IsConnected()) {
        DyingSocket->Close(CloseCode, CloseReason);
    }
}

/**
 * @brief 处理单条后台队列出队的二进制分片并在完整消息收齐后送入环形帧解析器
 * @param FragmentBytes 分片数据
 * @param bIsLastFragment 是否为当前 WebSocket 消息的最后一个分片
 */
void FWebSocketNetTransport::ProcessBinaryFragment(TConstArrayView<uint8> FragmentBytes, bool bIsLastFragment) {
    const FWebSocketFragmentOutcome Outcome = FragmentAssembler.AppendFragment(FragmentBytes, bIsLastFragment);
    if (Outcome.Status == EWebSocketFragmentStatus::FrameExceedsLimit) {
        TriggerProtocolCircuitBreaker(TEXT("WebSocket binary fragment exceeded maximum allowed frame size."));
        return;
    }
    if (Outcome.Status == EWebSocketFragmentStatus::MessageReady) {
        ReceiveRingBuffer.Append(Outcome.AssembledMessage);
        DrainDecodedFrames();
    }
}

/**
 * @brief 从环形解析器连续弹出所有完整帧并广播；遇协议违例立即熔断断连
 */
void FWebSocketNetTransport::DrainDecodedFrames() {
    while (CurrentState == ENetConnectionState::Connected) {
        const FNetFrameDecodeOutcome Outcome = ReceiveRingBuffer.TryPopPacket();
        if (Outcome.Status == ENetFrameParseStatus::NeedMoreData) {
            return;
        }
        if (Outcome.Status != ENetFrameParseStatus::CompleteFrame) {
            TriggerProtocolCircuitBreaker(
                FString::Printf(TEXT("Malformed wire frame rejected (status=%d)."), static_cast<int32>(Outcome.Status))
            );
            return;
        }
        PacketReceivedEvent.Broadcast(Outcome.Packet.Header, Outcome.Packet.Payload);
    }
}

/**
 * @brief 因对端发来畸形头或超限包触发协议级熔断保护
 * @param ErrorReason 熔断原因说明
 */
void FWebSocketNetTransport::TriggerProtocolCircuitBreaker(const FString& ErrorReason) {
    ++ActiveGeneration;
    ReleaseSocket(1002, ErrorReason, true);
    FragmentAssembler.Reset();
    ReceiveRingBuffer.Reset();
    SharedEventQueue->Empty();
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

