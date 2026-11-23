/**
 * @file MockNetTransportTest.cpp
 * @brief 单机纯内存回环驱动（FMockNetTransport）状态机、路由匹配、重入安全、主动推送与断线容错单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Message/GameNetworkMessageTypes.h"
#include "Transport/MockNetTransport.h"
#include "Types/NetErrorCodes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetMockTransportTest,
    "Game.Net.Core.MockTransport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证单机回环驱动全生命周期状态迁移、回调重入注销安全、请求-响应序列号回传、回声开关与远端闪断
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetMockTransportTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FMockNetTransport Transport;
    TArray<ENetConnectionState> ObservedStates;
    TArray<FNetPacket> ReceivedPackets;
    TArray<FString> ObservedErrors;

    Transport.OnStateChanged().AddLambda([&ObservedStates](ENetConnectionState NewState) {
        ObservedStates.Add(NewState);
    });
    Transport.OnPacketReceived().AddLambda([&ReceivedPackets](const FNetPacketHeader& Header, TConstArrayView<uint8> Payload) {
        ReceivedPackets.Emplace(Header, Payload);
    });
    Transport.OnTransportError().AddLambda([&ObservedErrors](const FString& ErrorText) {
        ObservedErrors.Add(ErrorText);
    });

    TestEqual(TEXT("初始状态为 Disconnected"), Transport.GetState(), ENetConnectionState::Disconnected);
    Transport.Disconnect(1000, TEXT("NoOp"));
    TestEqual(TEXT("未连接时调用 Disconnect 不产生状态变更"), ObservedStates.Num(), 0);
    Transport.SimulateRemoteDisconnect(TEXT("Ignored"));
    TestEqual(TEXT("未连接时模拟远端断线不产生错误"), ObservedErrors.Num(), 0);

    FNetPacketHeader DummyHeader;
    DummyHeader.MessageId = 10;
    const TGameStatusResult<void> SendDisconnectedRes = Transport.Send(DummyHeader, {});
    TestTrue(TEXT("未连接时 Send 返回失败态"), SendDisconnectedRes.IsFailure());
    TestEqual(TEXT("未连接错误码为 NotConnected"), SendDisconnectedRes.GetError().ErrorCode, Game::NetErrorCodes::NotConnected);
    TestFalse(TEXT("未连接时 SimulateServerPush 返回 false"), Transport.SimulateServerPush(10, {}));
    Transport.Tick(0.016f);

    const TGameStatusResult<void> EmptyEndpointRes = Transport.Connect(TEXT(""));
    TestTrue(TEXT("空端点地址连接被拒绝返回失败态"), EmptyEndpointRes.IsFailure());
    TestEqual(TEXT("空端点错误码为 InvalidEndpoint"), EmptyEndpointRes.GetError().ErrorCode, Game::NetErrorCodes::InvalidEndpoint);
    TestEqual(TEXT("空端点触发一次错误通知"), ObservedErrors.Num(), 1);

    Transport.SetAcceptConnections(false);
    const TGameStatusResult<void> RefusedRes = Transport.Connect(TEXT("mock://rejected"));
    TestTrue(TEXT("关闭连接许可时 Connect 返回失败态"), RefusedRes.IsFailure());
    TestEqual(TEXT("拒绝连接错误码为 ConnectionRefused"), RefusedRes.GetError().ErrorCode, Game::NetErrorCodes::ConnectionRefused);
    TestEqual(TEXT("拒绝连接经历 Connecting -> Disconnected 两次状态转移"), ObservedStates.Num(), 2);
    TestEqual(TEXT("拒绝连接后状态回到 Disconnected"), Transport.GetState(), ENetConnectionState::Disconnected);
    TestEqual(TEXT("拒绝连接触发错误回调"), ObservedErrors.Num(), 2);

    ObservedStates.Reset();
    Transport.SetAcceptConnections(true);
    TestTrue(TEXT("开启许可后成功建立虚拟回环连接"), Transport.Connect(TEXT("mock://standalone")).IsSuccess());
    const TGameStatusResult<void> DuplicateRes = Transport.Connect(TEXT("mock://duplicate"));
    TestTrue(TEXT("已连接状态下重复 Connect 返回失败态"), DuplicateRes.IsFailure());
    TestEqual(TEXT("重复连接错误码为 AlreadyConnected"), DuplicateRes.GetError().ErrorCode, Game::NetErrorCodes::AlreadyConnected);
    TestEqual(TEXT("连接成功后状态为 Connected"), Transport.GetState(), ENetConnectionState::Connected);
    TestEqual(TEXT("记录的端点地址精确匹配"), Transport.GetConnectedEndpoint(), FString(TEXT("mock://standalone")));
    TestEqual(TEXT("成功连接产生 Connecting -> Connected 两次状态转移"), ObservedStates.Num(), 2);

    FNetPacketHeader InvalidHeader;
    InvalidHeader.Magic = 0x0000;
    TestTrue(TEXT("非法魔数报文头在 Send 边界被拦截返回失败态"), Transport.Send(InvalidHeader, {}).IsFailure());
    TestEqual(TEXT("被拦截报文不计入已发送计数"), Transport.GetSentPacketCount(), 0);

    Transport.RegisterMockHandler(
        1001,
        2001,
        [](const FNetPacketHeader& ReqHeader, TConstArrayView<uint8> ReqPayload) -> TOptional<TArray<uint8>> {
            TArray<uint8> RspBytes;
            RspBytes.Add(static_cast<uint8>(ReqHeader.SequenceId & 0xFF));
            RspBytes.Add(ReqPayload.IsEmpty() ? 0 : static_cast<uint8>(ReqPayload[0] + 1));
            return RspBytes;
        }
    );
    Transport.RegisterMockHandler(
        1002,
        0,
        [](const FNetPacketHeader&, TConstArrayView<uint8>) -> TOptional<TArray<uint8>> {
            return NullOpt;
        }
    );
    Transport.RegisterMockHandler(
        1003,
        2003,
        [&Transport](const FNetPacketHeader&, TConstArrayView<uint8>) -> TOptional<TArray<uint8>> {
            Transport.UnregisterMockHandler(1003);
            return TArray<uint8>{0x7F};
        }
    );

    FNetPacketHeader BuyHeader;
    BuyHeader.MessageId = 1001;
    BuyHeader.SequenceId = 42;
    const uint8 BuyPayload[] = {9};
    TestTrue(TEXT("命中模拟处理器的请求发送成功"), Transport.Send(BuyHeader, BuyPayload).IsSuccess());
    TestEqual(TEXT("Tick 前响应尚未派发到主线程"), ReceivedPackets.Num(), 0);

    FNetPacketHeader OneWayHeader;
    OneWayHeader.MessageId = 1002;
    OneWayHeader.SequenceId = 43;
    TestTrue(TEXT("单向通知模拟请求发送成功"), Transport.Send(OneWayHeader, {}).IsSuccess());

    FNetPacketHeader ReentrantHeader;
    ReentrantHeader.MessageId = 1003;
    ReentrantHeader.SequenceId = 45;
    TestTrue(TEXT("回调内自注销路由的请求安全发送不悬垂"), Transport.Send(ReentrantHeader, {}).IsSuccess());

    FNetPacketHeader UnhandledHeader;
    UnhandledHeader.MessageId = 9999;
    UnhandledHeader.SequenceId = 44;
    const uint8 RawEchoBytes[] = {77, 88};
    Transport.SetEchoUnregisteredMessages(false);
    TestTrue(TEXT("关闭回声时未注册消息仍成功投递"), Transport.Send(UnhandledHeader, RawEchoBytes).IsSuccess());

    Transport.Tick(0.016f);
    TestEqual(TEXT("Tick 后收到 2001 与自注销路由 2003 共 2 条报文"), ReceivedPackets.Num(), 2);
    TestEqual(TEXT("响应协议 ID 为 2001"), ReceivedPackets[0].Header.MessageId, 2001u);
    TestEqual(TEXT("响应自动继承请求的 SequenceId 42"), ReceivedPackets[0].Header.SequenceId, 42u);
    TestEqual(TEXT("响应负载计算结果精确匹配"), ReceivedPackets[0].Payload[1], static_cast<uint8>(10));
    TestEqual(TEXT("回调内自注销路由仍准确填入预设响应 ID 2003"), ReceivedPackets[1].Header.MessageId, 2003u);
    TestEqual(TEXT("自注销路由响应负载精确还原"), ReceivedPackets[1].Payload[0], static_cast<uint8>(0x7F));

    Transport.SetEchoUnregisteredMessages(true);
    Transport.UnregisterMockHandler(1001);
    TestTrue(TEXT("注销处理器并开启回声后再次发送"), Transport.Send(BuyHeader, BuyPayload).IsSuccess());
    const uint8 PushBytes[] = {55};
    TestTrue(TEXT("模拟服务端主动推送成功入队"), Transport.SimulateServerPush(3001, PushBytes, 0));

    TArray<uint8> OversizedPush;
    OversizedPush.SetNumZeroed(static_cast<int32>(FNetPacketHeader::DEFAULT_MAX_PAYLOAD_BYTES) + 1);
    TestFalse(TEXT("超限负载的 SimulateServerPush 被安全拒绝"), Transport.SimulateServerPush(3002, OversizedPush, 0));

    Transport.Tick(0.016f);
    TestEqual(TEXT("收到回声报文与服务端主动推送共累计 4 条"), ReceivedPackets.Num(), 4);
    TestEqual(TEXT("回声报文保持原协议 ID 1001"), ReceivedPackets[2].Header.MessageId, 1001u);
    TestEqual(TEXT("主动推送协议 ID 为 3001"), ReceivedPackets[3].Header.MessageId, 3001u);
    TestEqual(TEXT("累计成功发送包数为 5"), Transport.GetSentPacketCount(), 5);

    Transport.ClearMockHandlers();
    Transport.SimulateRemoteDisconnect(TEXT("PeerReset"));
    TestEqual(TEXT("远端闪断后状态转为 Disconnected"), Transport.GetState(), ENetConnectionState::Disconnected);
    TestTrue(TEXT("远端闪断清空已连接端点"), Transport.GetConnectedEndpoint().IsEmpty());

    TestTrue(TEXT("闪断后允许重新建立连接"), Transport.Connect(TEXT("mock://reconnected")).IsSuccess());
    Transport.RegisterMockHandler(
        1004,
        2004,
        [&Transport](const FNetPacketHeader&, TConstArrayView<uint8>) -> TOptional<TArray<uint8>> {
            Transport.Disconnect(1000, TEXT("DisconnectInsideMockHandler"));
            return TArray<uint8>{0x42};
        }
    );
    FNetPacketHeader DropInHandlerHeader;
    DropInHandlerHeader.MessageId = 1004;
    DropInHandlerHeader.SequenceId = 99;
    const int32 ReceivedBeforeDrop = ReceivedPackets.Num();
    TestTrue(TEXT("回调内触发断线的请求 Send 本身返回成功"), Transport.Send(DropInHandlerHeader, {}).IsSuccess());
    TestEqual(TEXT("回调内 Disconnect 生效后状态转为 Disconnected"), Transport.GetState(), ENetConnectionState::Disconnected);
    TestTrue(TEXT("重连进入新会话"), Transport.Connect(TEXT("mock://after_drop")).IsSuccess());
    Transport.Tick(0.016f);
    TestEqual(TEXT("断线前回调返回的包不会残留污染新连接会话"), ReceivedPackets.Num(), ReceivedBeforeDrop);

    Transport.Disconnect(1000, TEXT("NormalShutdown"));
    TestEqual(TEXT("主动断开后状态为 Disconnected"), Transport.GetState(), ENetConnectionState::Disconnected);

    const uint8 EnvelopePayload[] = {1, 2, 3};
    FGameNetOutboundMessage OutMsg(100, 1, EnvelopePayload);
    TestEqual(TEXT("OutboundMessage MessageId 一致"), OutMsg.MessageId, 100u);
    TestEqual(TEXT("OutboundMessage SequenceId 一致"), OutMsg.SequenceId, 1u);
    TestEqual(TEXT("OutboundMessage 负载大小一致"), OutMsg.Payload.Num(), 3);
    FGameNetOutboundMessage OutMsgCopy = OutMsg;
    TestTrue(TEXT("OutboundMessage 相等运算符一致"), OutMsgCopy == OutMsg);
    OutMsgCopy.Reset();
    TestEqual(TEXT("OutboundMessage 重置后负载为空"), OutMsgCopy.Payload.Num(), 0);

    FGameNetInboundMessage InMsg(200, 2, EnvelopePayload);
    TestEqual(TEXT("InboundMessage MessageId 一致"), InMsg.MessageId, 200u);
    FGameNetInboundMessage InMsgCopy = InMsg;
    TestTrue(TEXT("InboundMessage 相等运算符一致"), InMsgCopy == InMsg);
    InMsgCopy.Reset();
    TestEqual(TEXT("InboundMessage 重置后负载为空"), InMsgCopy.Payload.Num(), 0);

    FGameNetStateMessage StateMsg(true, ENetConnectionState::Connected, TEXT("mock://test"));
    TestTrue(TEXT("StateMsg 连接状态为 true"), StateMsg.bIsConnected);
    TestEqual(TEXT("StateMsg 连接枚举一致"), StateMsg.ConnectionState, ENetConnectionState::Connected);
    TestEqual(TEXT("StateMsg 端点一致"), StateMsg.Endpoint, FString(TEXT("mock://test")));
    FGameNetStateMessage StateMsgCopy = StateMsg;
    TestTrue(TEXT("StateMsg 相等运算符一致"), StateMsgCopy == StateMsg);

    return true;
}

#endif
