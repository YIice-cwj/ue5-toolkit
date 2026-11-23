/**
 * @file WebSocketNetTransportTest.cpp
 * @brief WebSocket 传输驱动（FWebSocketNetTransport）有界无锁队列、单分片零拷贝、代际防串扰与协议熔断单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Codec/NetPacketCodec.h"
#include "IWebSocket.h"
#include "Transport/WebSocketNetTransport.h"

namespace {

/**
 * @brief 用于确定性离线测试的底层 IWebSocket 内存桩件
 */
class FFakeWebSocket final : public IWebSocket {
public:
    void Connect() override {
        bConnectCalled = true;
    }

    void Close(int32 Code, const FString& Reason) override {
        bCloseCalled = true;
        bConnected = false;
        LastCloseCode = Code;
        LastCloseReason = Reason;
    }

    bool IsConnected() override {
        return bConnected;
    }

    void Send([[maybe_unused]] const FString& Data) override {
    }

    void Send(const void* Data, SIZE_T Size, bool bIsBinary) override {
        bLastSendWasBinary = bIsBinary;
        LastSentFrame.Reset(static_cast<int32>(Size));
        if (Data != nullptr && Size > 0) {
            LastSentFrame.Append(static_cast<const uint8*>(Data), static_cast<int32>(Size));
        }
    }

    void SetTextMessageMemoryLimit([[maybe_unused]] uint64 TextMessageMemoryLimit) override {
    }

    FWebSocketConnectedEvent& OnConnected() override {
        return ConnectedEvent;
    }

    FWebSocketConnectionErrorEvent& OnConnectionError() override {
        return ConnectionErrorEvent;
    }

    FWebSocketClosedEvent& OnClosed() override {
        return ClosedEvent;
    }

    FWebSocketMessageEvent& OnMessage() override {
        return MessageEvent;
    }

    FWebSocketBinaryMessageEvent& OnBinaryMessage() override {
        return BinaryMessageEvent;
    }

    FWebSocketRawMessageEvent& OnRawMessage() override {
        return RawMessageEvent;
    }

    FWebSocketMessageSentEvent& OnMessageSent() override {
        return MessageSentEvent;
    }

    void TriggerConnected() {
        bConnected = true;
        ConnectedEvent.Broadcast();
    }

    void TriggerError(const FString& ErrorText) {
        bConnected = false;
        ConnectionErrorEvent.Broadcast(ErrorText);
    }

    void TriggerClosed(int32 Code, const FString& Reason, bool bClean) {
        bConnected = false;
        ClosedEvent.Broadcast(Code, Reason, bClean);
    }

    void TriggerBinaryFragment(TConstArrayView<uint8> Chunk, bool bLastFragment) {
        BinaryMessageEvent.Broadcast(Chunk.GetData(), static_cast<SIZE_T>(Chunk.Num()), bLastFragment);
    }

    void TriggerOversizedFragmentSize(SIZE_T FakeSize) {
        const uint8 Dummy = 0;
        BinaryMessageEvent.Broadcast(&Dummy, FakeSize, true);
    }

    void SnapshotBinaryEvent() {
        SavedStaleBinaryEvent = BinaryMessageEvent;
    }

    void TriggerSavedStaleBinaryFragment(TConstArrayView<uint8> Chunk, bool bLastFragment) const {
        SavedStaleBinaryEvent.Broadcast(Chunk.GetData(), static_cast<SIZE_T>(Chunk.Num()), bLastFragment);
    }

private:
    FWebSocketConnectedEvent ConnectedEvent;
    FWebSocketConnectionErrorEvent ConnectionErrorEvent;
    FWebSocketClosedEvent ClosedEvent;
    FWebSocketMessageEvent MessageEvent;
    FWebSocketBinaryMessageEvent BinaryMessageEvent;
    FWebSocketBinaryMessageEvent SavedStaleBinaryEvent;
    FWebSocketRawMessageEvent RawMessageEvent;
    FWebSocketMessageSentEvent MessageSentEvent;

public:
    bool bConnectCalled{false};
    bool bCloseCalled{false};
    bool bConnected{false};
    bool bLastSendWasBinary{false};
    int32 LastCloseCode{0};
    FString LastCloseReason;
    TArray<uint8> LastSentFrame;
};

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetWebSocketTransportTest,
    "Game.Net.Service.WebSocketTransport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 WebSocket 驱动的握手透传、无锁环形队列跨线程派发、单分片直通/多分片重组、IO 线程超限拦截、代际隔离与溢出熔断
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetWebSocketTransportTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSharedPtr<FFakeWebSocket> LatestFakeSocket;
    FString CapturedUrl;
    FString CapturedSubProtocol;
    TMap<FString, FString> CapturedHeaders;
    bool bReturnNullSocket = false;

    FWebSocketNetTransport Transport(
        128,
        [&LatestFakeSocket, &CapturedUrl, &CapturedSubProtocol, &CapturedHeaders, &bReturnNullSocket](
            const FString& Url,
            const FString& SubProto,
            const TMap<FString, FString>& Headers
        ) -> TSharedPtr<IWebSocket> {
            if (bReturnNullSocket) {
                return nullptr;
            }
            CapturedUrl = Url;
            CapturedSubProtocol = SubProto;
            CapturedHeaders = Headers;
            LatestFakeSocket = MakeShared<FFakeWebSocket>();
            return LatestFakeSocket;
        }
    );

    TArray<ENetConnectionState> States;
    TArray<FNetPacket> Packets;
    TArray<FString> Errors;

    Transport.OnStateChanged().AddLambda([&States](ENetConnectionState NewState) {
        States.Add(NewState);
    });
    Transport.OnPacketReceived().AddLambda([&Packets](const FNetPacketHeader& Header, TConstArrayView<uint8> Payload) {
        Packets.Emplace(Header, Payload);
    });
    Transport.OnTransportError().AddLambda([&Errors](const FString& Err) {
        Errors.Add(Err);
    });

    TestTrue(TEXT("空 URL 连接被拒绝返回失败态"), Transport.Connect(TEXT("")).IsFailure());
    TestEqual(TEXT("空 URL 触发错误回调"), Errors.Num(), 1);

    bReturnNullSocket = true;
    TestTrue(TEXT("底层实例创建失败时 Connect 返回失败态"), Transport.Connect(TEXT("wss://gate.example.com/ws")).IsFailure());
    TestEqual(TEXT("底层创建失败触发错误回调"), Errors.Num(), 2);
    bReturnNullSocket = false;

    Transport.SetSubProtocol(TEXT("uetoolkit-v1"));
    TMap<FString, FString> CustomHeaders;
    CustomHeaders.Add(TEXT("X-Client-Region"), TEXT("CN"));
    Transport.SetUpgradeHeaders(CustomHeaders);

    TestTrue(TEXT("正常发起 WebSocket 异步握手"), Transport.Connect(TEXT("wss://gate.example.com/ws")).IsSuccess());
    TestTrue(TEXT("握手进行中重复调用 Connect 被拒绝返回失败态"), Transport.Connect(TEXT("wss://other")).IsFailure());
    TestEqual(TEXT("当前状态为 Connecting"), Transport.GetState(), ENetConnectionState::Connecting);
    TestEqual(TEXT("工厂收到完整 URL"), CapturedUrl, FString(TEXT("wss://gate.example.com/ws")));
    TestEqual(TEXT("工厂收到子协议"), CapturedSubProtocol, FString(TEXT("uetoolkit-v1")));
    TestEqual(TEXT("工厂收到握手扩展头"), CapturedHeaders.FindRef(TEXT("X-Client-Region")), FString(TEXT("CN")));
    TestTrue(TEXT("底层 Connect 被触发"), LatestFakeSocket.IsValid() && LatestFakeSocket->bConnectCalled);

    Transport.Disconnect(1000, TEXT("CancelHandshake"));
    TestTrue(TEXT("Connecting 阶段主动 Disconnect 仍须调用底层 Close 中止握手"), LatestFakeSocket->bCloseCalled);
    TestEqual(TEXT("中止握手后状态为 Disconnected"), Transport.GetState(), ENetConnectionState::Disconnected);

    TestTrue(TEXT("重新发起 WebSocket 握手"), Transport.Connect(TEXT("wss://gate.example.com/ws")).IsSuccess());
    LatestFakeSocket->TriggerConnected();
    TestEqual(TEXT("Tick 前主线程状态仍维持 Connecting（严格跨线程隔离）"), Transport.GetState(), ENetConnectionState::Connecting);
    Transport.Tick(0.016f);
    TestEqual(TEXT("Tick 消费队列后主线程状态迁移为 Connected"), Transport.GetState(), ENetConnectionState::Connected);

    FNetPacketHeader OutHeader;
    OutHeader.MessageId = 7001;
    OutHeader.SequenceId = 9;
    const uint8 OutBytes[] = {11, 22, 33};
    TestTrue(TEXT("已连接状态下二进制发包成功"), Transport.Send(OutHeader, OutBytes).IsSuccess());
    TestTrue(TEXT("底层以 Binary 模式发送帧"), LatestFakeSocket->bLastSendWasBinary);
    TestEqual(TEXT("发送的线格式总长度为 19 字节"), LatestFakeSocket->LastSentFrame.Num(), 19);

    FNetPacketHeader OversizedOutHeader;
    OversizedOutHeader.MessageId = 7002;
    TArray<uint8> OversizedPayload;
    OversizedPayload.SetNumZeroed(200);
    TestTrue(TEXT("超出单包 128B 阈值的发送请求在本地边界被拦截返回失败态"), Transport.Send(OversizedOutHeader, OversizedPayload).IsFailure());

    FNetPacketHeader InHeader1;
    InHeader1.MessageId = 8001;
    InHeader1.SequenceId = 1;
    const uint8 InPayload1[] = {1, 2, 3, 4, 5, 6};
    TArray<uint8> Frame1;
    FNetPacketCodec::EncodeFrameTo(InHeader1, InPayload1, Frame1);

    FNetPacketHeader InHeader2;
    InHeader2.MessageId = 8002;
    InHeader2.SequenceId = 2;
    const uint8 InPayload2[] = {99};
    TArray<uint8> Frame2;
    FNetPacketCodec::EncodeFrameTo(InHeader2, InPayload2, Frame2);

    LatestFakeSocket->TriggerBinaryFragment(TConstArrayView<uint8>(Frame1.GetData(), 7), false);
    LatestFakeSocket->TriggerBinaryFragment(TConstArrayView<uint8>(Frame1.GetData() + 7, 8), false);
    LatestFakeSocket->TriggerBinaryFragment(TConstArrayView<uint8>(Frame1.GetData() + 15, Frame1.Num() - 15), true);
    LatestFakeSocket->TriggerBinaryFragment(Frame2, true);

    TArray<uint8> StickyTwoFrames = Frame1;
    StickyTwoFrames.Append(Frame2);
    LatestFakeSocket->TriggerBinaryFragment(TConstArrayView<uint8>(StickyTwoFrames.GetData(), Frame1.Num() + 6), true);
    LatestFakeSocket->TriggerBinaryFragment(
        TConstArrayView<uint8>(StickyTwoFrames.GetData() + Frame1.Num() + 6, StickyTwoFrames.Num() - Frame1.Num() - 6),
        true
    );

    Transport.Tick(0.016f);
    TestEqual(TEXT("多分片、单分片直通与跨 WS 半包均完整解出（共 4 帧）"), Packets.Num(), 4);
    TestEqual(TEXT("首包协议 ID 为 8001"), Packets[0].Header.MessageId, 8001u);
    TestEqual(TEXT("首包 6 字节负载无损还原"), Packets[0].Payload.Num(), 6);
    TestEqual(TEXT("次包协议 ID 为 8002"), Packets[1].Header.MessageId, 8002u);
    TestEqual(TEXT("粘包+半包重组末包协议 ID 为 8002"), Packets[3].Header.MessageId, 8002u);

    LatestFakeSocket->TriggerOversizedFragmentSize(static_cast<SIZE_T>(1024 * 1024));
    Transport.Tick(0.016f);
    TestEqual(TEXT("IO 线程超限单分片零分配拦截并触发熔断断开"), Transport.GetState(), ENetConnectionState::Disconnected);
    TestEqual(TEXT("底层 Socket 收到协议错误关闭码 1002"), LatestFakeSocket->LastCloseCode, 1002);

    TestTrue(TEXT("重连验证多分片累计超限熔断"), Transport.Connect(TEXT("wss://gate.example.com/ws")).IsSuccess());
    LatestFakeSocket->TriggerConnected();
    Transport.Tick(0.016f);
    TArray<uint8> HundredBytes;
    HundredBytes.SetNumZeroed(100);
    LatestFakeSocket->TriggerBinaryFragment(HundredBytes, false);
    LatestFakeSocket->TriggerBinaryFragment(HundredBytes, true);
    Transport.Tick(0.016f);
    TestEqual(TEXT("多分片累计超限触发 FrameExceedsLimit 熔断断开"), Transport.GetState(), ENetConnectionState::Disconnected);

    TestTrue(TEXT("重连验证畸形魔数熔断"), Transport.Connect(TEXT("wss://gate.example.com/ws")).IsSuccess());
    LatestFakeSocket->TriggerConnected();
    Transport.Tick(0.016f);
    uint8 PoisonBytes[FNetPacketHeader::WIRE_HEADER_BYTES];
    FNetPacketHeader PoisonHeader;
    PoisonHeader.Magic = 0xBEEF;
    FNetPacketCodec::EncodeHeaderTo(PoisonHeader, PoisonBytes);
    LatestFakeSocket->TriggerBinaryFragment(PoisonBytes, true);
    Transport.Tick(0.016f);
    TestEqual(TEXT("畸形帧立即触发熔断回到 Disconnected"), Transport.GetState(), ENetConnectionState::Disconnected);

    TestTrue(TEXT("重连验证握手失败与正常关闭不误报错误"), Transport.Connect(TEXT("wss://gate.example.com/ws")).IsSuccess());
    LatestFakeSocket->TriggerError(TEXT("HandshakeRefused"));
    Transport.Tick(0.016f);
    TestEqual(TEXT("握手失败后状态迁移回 Disconnected"), Transport.GetState(), ENetConnectionState::Disconnected);

    TestTrue(TEXT("重连验证远端干净关闭"), Transport.Connect(TEXT("wss://gate.example.com/ws")).IsSuccess());
    LatestFakeSocket->TriggerConnected();
    Transport.Tick(0.016f);
    const int32 ErrorsBeforeCleanClose = Errors.Num();
    LatestFakeSocket->TriggerClosed(1000, TEXT("CleanServerShutdown"), true);
    Transport.Tick(0.016f);
    TestEqual(TEXT("干净关闭后回到 Disconnected"), Transport.GetState(), ENetConnectionState::Disconnected);
    TestEqual(TEXT("干净关闭不触发 OnTransportError"), Errors.Num(), ErrorsBeforeCleanClose);

    TestTrue(TEXT("重连验证远端异常关闭"), Transport.Connect(TEXT("wss://gate.example.com/ws")).IsSuccess());
    LatestFakeSocket->TriggerConnected();
    LatestFakeSocket->TriggerClosed(1006, TEXT("AbnormalDrop"), false);
    Transport.Tick(0.016f);
    TestEqual(TEXT("异常关闭后回到 Disconnected"), Transport.GetState(), ENetConnectionState::Disconnected);
    TestEqual(TEXT("异常关闭触发 1 次 OnTransportError"), Errors.Num(), ErrorsBeforeCleanClose + 1);

    TestTrue(TEXT("重连验证有界无锁事件队列溢出保护熔断"), Transport.Connect(TEXT("wss://gate.example.com/ws")).IsSuccess());
    LatestFakeSocket->TriggerConnected();
    Transport.Tick(0.016f);
    const TSharedPtr<FFakeWebSocket> StaleSocket = LatestFakeSocket;
    StaleSocket->SnapshotBinaryEvent();
    for (uint32 FloodIdx = 0; FloodIdx < FWebSocketNetTransport::INBOUND_EVENT_RING_CAPACITY + 10; ++FloodIdx) {
        LatestFakeSocket->TriggerBinaryFragment(Frame2, true);
    }
    Transport.Tick(0.016f);
    TestEqual(TEXT("有界事件队列溢出触发熔断断开防止静默丢帧或 OOM"), Transport.GetState(), ENetConnectionState::Disconnected);

    TestTrue(TEXT("重建新连接验证旧代际滞后溢出绝不污染新会话"), Transport.Connect(TEXT("wss://gate.example.com/ws")).IsSuccess());
    LatestFakeSocket->TriggerConnected();
    Transport.Tick(0.016f);
    TestEqual(TEXT("新连接正常建立"), Transport.GetState(), ENetConnectionState::Connected);
    for (uint32 StaleFloodIdx = 0; StaleFloodIdx < FWebSocketNetTransport::INBOUND_EVENT_RING_CAPACITY + 10; ++StaleFloodIdx) {
        StaleSocket->TriggerSavedStaleBinaryFragment(Frame2, true);
    }
    const int32 PacketsBeforeFresh = Packets.Num();
    LatestFakeSocket->TriggerBinaryFragment(Frame2, true);
    Transport.Tick(0.016f);
    TestEqual(TEXT("旧通道滞后溢出被物理隔离，新连接保持 Connected"), Transport.GetState(), ENetConnectionState::Connected);
    TestEqual(TEXT("新连接仍正常接收并派发新数据包"), Packets.Num(), PacketsBeforeFresh + 1);

    bool bReconnectedDuringStickyDispatch = false;
    int32 CrossGenerationLeakCount = 0;
    const FDelegateHandle StickyGuardHandle = Transport.OnPacketReceived().AddLambda(
        [&Transport, &LatestFakeSocket, &bReconnectedDuringStickyDispatch, &CrossGenerationLeakCount](
            const FNetPacketHeader& Header,
            TConstArrayView<uint8>
        ) {
            if (!bReconnectedDuringStickyDispatch && Header.MessageId == 8001u) {
                bReconnectedDuringStickyDispatch = true;
                Transport.Disconnect(1000, TEXT("SwitchDuringStickyFrame"));
                (void)Transport.Connect(TEXT("wss://gate.example.com/ws_gen2"));
                LatestFakeSocket->TriggerConnected();
                Transport.Tick(0.016f);
                return;
            }
            if (bReconnectedDuringStickyDispatch && Header.MessageId == 8002u) {
                ++CrossGenerationLeakCount;
            }
        }
    );
    LatestFakeSocket->TriggerBinaryFragment(StickyTwoFrames, true);
    Transport.Tick(0.016f);
    Transport.OnPacketReceived().Remove(StickyGuardHandle);
    TestTrue(TEXT("粘包首帧回调内触发重入断连与重建成功"), bReconnectedDuringStickyDispatch);
    TestEqual(TEXT("重入切代际后旧粘包次帧 8002 绝不跨代际泄漏"), CrossGenerationLeakCount, 0);
    TestEqual(TEXT("重建的新代际连接保持 Connected"), Transport.GetState(), ENetConnectionState::Connected);

    return true;
}

#endif
