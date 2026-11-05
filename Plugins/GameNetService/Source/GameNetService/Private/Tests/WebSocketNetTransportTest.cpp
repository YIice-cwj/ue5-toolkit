/**
 * @file WebSocketNetTransportTest.cpp
 * @brief WebSocket 传输驱动（FWebSocketNetTransport）无锁跨线程队列、分片重组、粘包解包与协议熔断单元测试
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
    /**
     * @brief 记录握手连接调用
     */
    void Connect() override {
        bConnectCalled = true;
    }

    /**
     * @brief 记录主动关闭调用与关闭参数
     * @param Code 关闭码
     * @param Reason 关闭原因
     */
    void Close(int32 Code, const FString& Reason) override {
        bConnected = false;
        LastCloseCode = Code;
        LastCloseReason = Reason;
    }

    /**
     * @brief 查询模拟连接态
     * @return 当前模拟连接态
     */
    bool IsConnected() override {
        return bConnected;
    }

    /**
     * @brief 文本发送空实现
     * @param Data 文本内容
     */
    void Send([[maybe_unused]] const FString& Data) override {
    }

    /**
     * @brief 记录发出的二进制线格式字节流
     * @param Data 数据首地址
     * @param Size 字节长度
     * @param bIsBinary 是否二进制模式
     */
    void Send(const void* Data, SIZE_T Size, bool bIsBinary) override {
        bLastSendWasBinary = bIsBinary;
        LastSentFrame.Reset(static_cast<int32>(Size));
        if (Data != nullptr && Size > 0) {
            LastSentFrame.Append(static_cast<const uint8*>(Data), static_cast<int32>(Size));
        }
    }

    /**
     * @brief 文本内存上限设置空实现
     * @param TextMessageMemoryLimit 字节上限
     */
    void SetTextMessageMemoryLimit([[maybe_unused]] uint64 TextMessageMemoryLimit) override {
    }

    /**
     * @brief 获取握手完成事件
     * @return 事件委托引用
     */
    FWebSocketConnectedEvent& OnConnected() override {
        return ConnectedEvent;
    }

    /**
     * @brief 获取握手错误事件
     * @return 事件委托引用
     */
    FWebSocketConnectionErrorEvent& OnConnectionError() override {
        return ConnectionErrorEvent;
    }

    /**
     * @brief 获取关闭事件
     * @return 事件委托引用
     */
    FWebSocketClosedEvent& OnClosed() override {
        return ClosedEvent;
    }

    /**
     * @brief 获取文本消息事件
     * @return 事件委托引用
     */
    FWebSocketMessageEvent& OnMessage() override {
        return MessageEvent;
    }

    /**
     * @brief 获取二进制消息分片事件
     * @return 事件委托引用
     */
    FWebSocketBinaryMessageEvent& OnBinaryMessage() override {
        return BinaryMessageEvent;
    }

    /**
     * @brief 获取原始消息事件
     * @return 事件委托引用
     */
    FWebSocketRawMessageEvent& OnRawMessage() override {
        return RawMessageEvent;
    }

    /**
     * @brief 获取发送完成事件
     * @return 事件委托引用
     */
    FWebSocketMessageSentEvent& OnMessageSent() override {
        return MessageSentEvent;
    }

    /**
     * @brief 模拟后台线程触发连接成功回调
     */
    void TriggerConnected() {
        bConnected = true;
        ConnectedEvent.Broadcast();
    }

    /**
     * @brief 模拟后台线程触发连接错误回调
     * @param ErrorText 错误描述
     */
    void TriggerError(const FString& ErrorText) {
        bConnected = false;
        ConnectionErrorEvent.Broadcast(ErrorText);
    }

    /**
     * @brief 模拟后台线程触发连接关闭回调
     * @param Code 关闭码
     * @param Reason 关闭原因
     * @param bClean 是否正常关闭
     */
    void TriggerClosed(int32 Code, const FString& Reason, bool bClean) {
        bConnected = false;
        ClosedEvent.Broadcast(Code, Reason, bClean);
    }

    /**
     * @brief 模拟后台线程推送一段二进制分片
     * @param Chunk 分片数据切片
     * @param bLastFragment 是否末分片
     */
    void TriggerBinaryFragment(TConstArrayView<uint8> Chunk, bool bLastFragment) {
        BinaryMessageEvent.Broadcast(Chunk.GetData(), static_cast<SIZE_T>(Chunk.Num()), bLastFragment);
    }

private:
    FWebSocketConnectedEvent ConnectedEvent;
    FWebSocketConnectionErrorEvent ConnectionErrorEvent;
    FWebSocketClosedEvent ClosedEvent;
    FWebSocketMessageEvent MessageEvent;
    FWebSocketBinaryMessageEvent BinaryMessageEvent;
    FWebSocketRawMessageEvent RawMessageEvent;
    FWebSocketMessageSentEvent MessageSentEvent;

public:
    bool bConnectCalled{false};
    bool bConnected{false};
    bool bLastSendWasBinary{false};
    int32 LastCloseCode{0};
    FString LastCloseReason;
    TArray<uint8> LastSentFrame;
};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetWebSocketTransportTest,
    "Game.Net.Service.WebSocketTransport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 WebSocket 驱动的握手透传、MPSC 主线程派发、多分片拼装、粘包连续提取与畸形包熔断断连
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
        FNetPacket Copy;
        Copy.Header = Header;
        Copy.Payload.Append(Payload.GetData(), Payload.Num());
        Packets.Add(MoveTemp(Copy));
    });
    Transport.OnTransportError().AddLambda([&Errors](const FString& Err) {
        Errors.Add(Err);
    });

    TestFalse(TEXT("空 URL 连接被拒绝"), Transport.Connect(TEXT("")));
    TestEqual(TEXT("空 URL 触发错误回调"), Errors.Num(), 1);

    bReturnNullSocket = true;
    TestFalse(TEXT("底层实例创建失败时 Connect 返回 false"), Transport.Connect(TEXT("wss://gate.example.com/ws")));
    TestEqual(TEXT("底层创建失败触发错误回调"), Errors.Num(), 2);
    bReturnNullSocket = false;

    Transport.SetSubProtocol(TEXT("uetoolkit-v1"));
    TMap<FString, FString> CustomHeaders;
    CustomHeaders.Add(TEXT("X-Client-Region"), TEXT("CN"));
    Transport.SetUpgradeHeaders(CustomHeaders);

    TestTrue(TEXT("正常发起 WebSocket 异步握手"), Transport.Connect(TEXT("wss://gate.example.com/ws")));
    TestFalse(TEXT("握手进行中重复调用 Connect 被拒绝"), Transport.Connect(TEXT("wss://other")));
    TestEqual(TEXT("当前状态为 Connecting"), Transport.GetState(), ENetConnectionState::Connecting);
    TestEqual(TEXT("工厂收到完整 URL"), CapturedUrl, FString(TEXT("wss://gate.example.com/ws")));
    TestEqual(TEXT("工厂收到子协议"), CapturedSubProtocol, FString(TEXT("uetoolkit-v1")));
    TestEqual(TEXT("工厂收到握手扩展头"), CapturedHeaders.FindRef(TEXT("X-Client-Region")), FString(TEXT("CN")));
    TestTrue(TEXT("底层 Connect 被触发"), LatestFakeSocket.IsValid() && LatestFakeSocket->bConnectCalled);

    LatestFakeSocket->TriggerConnected();
    TestEqual(TEXT("Tick 前主线程状态仍维持 Connecting（严格跨线程隔离）"), Transport.GetState(), ENetConnectionState::Connecting);
    Transport.Tick(0.016f);
    TestEqual(TEXT("Tick 消费队列后主线程状态迁移为 Connected"), Transport.GetState(), ENetConnectionState::Connected);

    FNetPacketHeader OutHeader;
    OutHeader.MessageId = 7001;
    OutHeader.SequenceId = 9;
    const uint8 OutBytes[] = {11, 22, 33};
    TestTrue(TEXT("已连接状态下二进制发包成功"), Transport.Send(OutHeader, OutBytes));
    TestTrue(TEXT("底层以 Binary 模式发送帧"), LatestFakeSocket->bLastSendWasBinary);
    TestEqual(TEXT("发送的线格式总长度为 19 字节"), LatestFakeSocket->LastSentFrame.Num(), 19);

    FNetPacketHeader OversizedOutHeader;
    OversizedOutHeader.MessageId = 7002;
    TArray<uint8> OversizedPayload;
    OversizedPayload.SetNumZeroed(200);
    TestFalse(TEXT("超出单包 128B 阈值的发送请求在本地边界被拦截"), Transport.Send(OversizedOutHeader, OversizedPayload));

    FNetPacketHeader InHeader1;
    InHeader1.MessageId = 8001;
    InHeader1.SequenceId = 1;
    const uint8 InPayload1[] = {1, 2, 3, 4, 5, 6};
    const TArray<uint8> Frame1 = FNetPacketCodec::EncodeFrame(InHeader1, InPayload1);

    FNetPacketHeader InHeader2;
    InHeader2.MessageId = 8002;
    InHeader2.SequenceId = 2;
    const uint8 InPayload2[] = {99};
    const TArray<uint8> Frame2 = FNetPacketCodec::EncodeFrame(InHeader2, InPayload2);

    LatestFakeSocket->TriggerBinaryFragment(TConstArrayView<uint8>(Frame1.GetData(), 7), false);
    LatestFakeSocket->TriggerBinaryFragment(TConstArrayView<uint8>(Frame1.GetData() + 7, 8), false);
    LatestFakeSocket->TriggerBinaryFragment(TConstArrayView<uint8>(Frame1.GetData() + 15, Frame1.Num() - 15), true);
    LatestFakeSocket->TriggerBinaryFragment(Frame2, true);

    Transport.Tick(0.016f);
    TestEqual(TEXT("多分片消息与单分片消息均完整解出"), Packets.Num(), 2);
    TestEqual(TEXT("首包协议 ID 为 8001"), Packets[0].Header.MessageId, 8001u);
    TestEqual(TEXT("首包 6 字节负载无损还原"), Packets[0].Payload.Num(), 6);
    TestEqual(TEXT("次包协议 ID 为 8002"), Packets[1].Header.MessageId, 8002u);

    FNetPacketHeader PoisonHeader;
    PoisonHeader.Magic = 0xBEEF;
    LatestFakeSocket->TriggerBinaryFragment(FNetPacketCodec::EncodeHeader(PoisonHeader), true);
    Transport.Tick(0.016f);
    TestEqual(TEXT("畸形帧立即触发熔断回到 Disconnected"), Transport.GetState(), ENetConnectionState::Disconnected);
    TestEqual(TEXT("底层 Socket 收到协议错误关闭码 1002"), LatestFakeSocket->LastCloseCode, 1002);

    TestTrue(TEXT("熔断后重新发起连接"), Transport.Connect(TEXT("wss://gate.example.com/ws")));
    LatestFakeSocket->TriggerError(TEXT("HandshakeRefused"));
    Transport.Tick(0.016f);
    TestEqual(TEXT("握手失败后状态迁移回 Disconnected"), Transport.GetState(), ENetConnectionState::Disconnected);

    TestTrue(TEXT("再次连接验证远端异常关闭"), Transport.Connect(TEXT("wss://gate.example.com/ws")));
    LatestFakeSocket->TriggerConnected();
    LatestFakeSocket->TriggerClosed(1006, TEXT("AbnormalDrop"), false);
    Transport.Tick(0.016f);
    TestEqual(TEXT("异常关闭后回到 Disconnected"), Transport.GetState(), ENetConnectionState::Disconnected);

    TestTrue(TEXT("再次连接验证主动 Disconnect"), Transport.Connect(TEXT("wss://gate.example.com/ws")));
    LatestFakeSocket->TriggerConnected();
    Transport.Tick(0.016f);
    Transport.Disconnect(1000, TEXT("Shutdown"));
    TestEqual(TEXT("主动断开后状态为 Disconnected"), Transport.GetState(), ENetConnectionState::Disconnected);

    return true;
}

#endif

