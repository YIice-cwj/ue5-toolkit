/**
 * @file MockNetTransportTest.cpp
 * @brief 单机纯内存回环驱动（FMockNetTransport）状态机、路由匹配、主动推送与断线容错单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Transport/MockNetTransport.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetMockTransportTest,
    "Game.Net.Core.MockTransport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证单机回环驱动全生命周期状态迁移、请求-响应序列号回传、回声开关与远端闪断
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
        FNetPacket Copy;
        Copy.Header = Header;
        Copy.Payload.Append(Payload.GetData(), Payload.Num());
        ReceivedPackets.Add(MoveTemp(Copy));
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
    TestFalse(TEXT("未连接时 Send 返回 false"), Transport.Send(DummyHeader, {}));
    TestFalse(TEXT("未连接时 SimulateServerPush 返回 false"), Transport.SimulateServerPush(10, {}));
    Transport.Tick(0.016f);

    TestFalse(TEXT("空端点地址连接被拒绝"), Transport.Connect(TEXT("")));
    TestEqual(TEXT("空端点触发一次错误通知"), ObservedErrors.Num(), 1);

    Transport.SetAcceptConnections(false);
    TestFalse(TEXT("关闭连接许可时 Connect 返回 false"), Transport.Connect(TEXT("mock://rejected")));
    TestEqual(TEXT("拒绝连接经历 Connecting -> Disconnected 两次状态转移"), ObservedStates.Num(), 2);
    TestEqual(TEXT("拒绝连接后状态回到 Disconnected"), Transport.GetState(), ENetConnectionState::Disconnected);
    TestEqual(TEXT("拒绝连接触发错误回调"), ObservedErrors.Num(), 2);

    ObservedStates.Reset();
    Transport.SetAcceptConnections(true);
    TestTrue(TEXT("开启许可后成功建立虚拟回环连接"), Transport.Connect(TEXT("mock://standalone")));
    TestFalse(TEXT("已连接状态下重复 Connect 返回 false"), Transport.Connect(TEXT("mock://duplicate")));
    TestEqual(TEXT("连接成功后状态为 Connected"), Transport.GetState(), ENetConnectionState::Connected);
    TestEqual(TEXT("记录的端点地址精确匹配"), Transport.GetConnectedEndpoint(), FString(TEXT("mock://standalone")));
    TestEqual(TEXT("成功连接产生 Connecting -> Connected 两次状态转移"), ObservedStates.Num(), 2);

    FNetPacketHeader InvalidHeader;
    InvalidHeader.Magic = 0x0000;
    TestFalse(TEXT("非法魔数报文头在 Send 边界被拦截"), Transport.Send(InvalidHeader, {}));
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

    FNetPacketHeader BuyHeader;
    BuyHeader.MessageId = 1001;
    BuyHeader.SequenceId = 42;
    const uint8 BuyPayload[] = {9};
    TestTrue(TEXT("命中模拟处理器的请求发送成功"), Transport.Send(BuyHeader, BuyPayload));
    TestEqual(TEXT("Tick 前响应尚未派发到主线程"), ReceivedPackets.Num(), 0);

    FNetPacketHeader OneWayHeader;
    OneWayHeader.MessageId = 1002;
    OneWayHeader.SequenceId = 43;
    TestTrue(TEXT("单向通知模拟请求发送成功"), Transport.Send(OneWayHeader, {}));

    FNetPacketHeader UnhandledHeader;
    UnhandledHeader.MessageId = 9999;
    UnhandledHeader.SequenceId = 44;
    const uint8 RawEchoBytes[] = {77, 88};
    Transport.SetEchoUnregisteredMessages(false);
    TestTrue(TEXT("关闭回声时未注册消息仍成功投递"), Transport.Send(UnhandledHeader, RawEchoBytes));

    Transport.Tick(0.016f);
    TestEqual(TEXT("Tick 后仅收到有响应的 2001 报文"), ReceivedPackets.Num(), 1);
    TestEqual(TEXT("响应协议 ID 为 2001"), ReceivedPackets[0].Header.MessageId, 2001u);
    TestEqual(TEXT("响应自动继承请求的 SequenceId 42"), ReceivedPackets[0].Header.SequenceId, 42u);
    TestEqual(TEXT("响应负载计算结果精确匹配"), ReceivedPackets[0].Payload[1], static_cast<uint8>(10));

    Transport.SetEchoUnregisteredMessages(true);
    Transport.UnregisterMockHandler(1001);
    TestTrue(TEXT("注销处理器并开启回声后再次发送"), Transport.Send(BuyHeader, BuyPayload));
    const uint8 PushBytes[] = {55};
    TestTrue(TEXT("模拟服务端主动推送成功入队"), Transport.SimulateServerPush(3001, PushBytes, 0));

    Transport.Tick(0.016f);
    TestEqual(TEXT("收到回声报文与服务端主动推送共累计 3 条"), ReceivedPackets.Num(), 3);
    TestEqual(TEXT("回声报文保持原协议 ID 1001"), ReceivedPackets[1].Header.MessageId, 1001u);
    TestEqual(TEXT("主动推送协议 ID 为 3001"), ReceivedPackets[2].Header.MessageId, 3001u);
    TestEqual(TEXT("累计成功发送包数为 4"), Transport.GetSentPacketCount(), 4);

    Transport.ClearMockHandlers();
    Transport.SimulateRemoteDisconnect(TEXT("PeerReset"));
    TestEqual(TEXT("远端闪断后状态转为 Disconnected"), Transport.GetState(), ENetConnectionState::Disconnected);
    TestTrue(TEXT("远端闪断清空已连接端点"), Transport.GetConnectedEndpoint().IsEmpty());

    TestTrue(TEXT("闪断后允许重新建立连接"), Transport.Connect(TEXT("mock://reconnected")));
    Transport.Disconnect(1000, TEXT("NormalShutdown"));
    TestEqual(TEXT("主动断开后状态为 Disconnected"), Transport.GetState(), ENetConnectionState::Disconnected);

    return true;
}

#endif

