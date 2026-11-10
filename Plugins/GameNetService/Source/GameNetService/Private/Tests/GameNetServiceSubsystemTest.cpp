/**
 * @file GameNetServiceSubsystemTest.cpp
 * @brief 业务网络门面子系统（UGameNetServiceSubsystem）端到端编排测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Message/GameNetworkMessageTypes.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Remote/GameRemoteLogTypes.h"
#include "Serializer/MemoryArchiveNetSerializer.h"
#include "System/GameNetServiceSubsystem.h"
#include "Tests/GameNetCoreTestTypes.h"
#include "Transport/MockNetTransport.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetServiceSubsystemEndToEndTest,
    "Game.Net.Service.SubsystemEndToEnd",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证单机回环全链路业务 RPC、GameCore 消息总线零耦合收发以及断线退避自动重连恢复
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetServiceSubsystemEndToEndTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameNetServiceSubsystemTestWorld")));

    UGameNetServiceSubsystem& NetService = UGameNetServiceSubsystem::Get(GameInstance);
    UGameplayMessageSubsystem& MessageBus = UGameplayMessageSubsystem::Get(GameInstance->GetWorld());
    TestEqual(
        TEXT("通过 World 与 GameInstance 解析同一个网络子系统实例"),
        UGameNetServiceSubsystem::GetPtr(GameInstance->GetWorld()),
        &NetService
    );
    TestNull(TEXT("空上下文指针安全返回 nullptr"), UGameNetServiceSubsystem::GetPtr(nullptr));

    const TSharedRef<FMockNetTransport> MockTransport = MakeShared<FMockNetTransport>();
    NetService.SetTransport(MockTransport);
    TestEqual(
        TEXT("GetTransport 返回已注入的传输实例"),
        NetService.GetTransport().Get(),
        static_cast<INetTransport*>(&MockTransport.Get())
    );

    TArray<FGameNetStateMessage> BusStateChanges;
    TArray<FGameNetInboundMessage> BusInboundMessages;
    FGameplayMessageListenerHandle StateBusHandle = MessageBus.RegisterListener<FGameNetStateMessage>(
        TAG_Game_Net_StateChanged,
        [&BusStateChanges](FGameplayTag, const FGameNetStateMessage& Msg) {
            BusStateChanges.Add(Msg);
        }
    );
    FGameplayMessageListenerHandle InboundBusHandle = MessageBus.RegisterListener<FGameNetInboundMessage>(
        TAG_Game_Net_Inbound,
        [&BusInboundMessages](FGameplayTag, const FGameNetInboundMessage& Msg) {
            BusInboundMessages.Add(Msg);
        }
    );

    MockTransport->RegisterMockHandler(
        1,
        2,
        [](const FNetPacketHeader&, TConstArrayView<uint8>) -> TOptional<TArray<uint8>> {
            return TArray<uint8>{};
        }
    );

    MockTransport->RegisterMockHandler(
        5001,
        5002,
        [&NetService](const FNetPacketHeader&, TConstArrayView<uint8> ReqBytes) -> TOptional<TArray<uint8>> {
            const TGameStatusResult<FGameNetTestPurchaseReq> Req =
                NetService.GetSerializer()->DeserializeStruct<FGameNetTestPurchaseReq>(ReqBytes);
            if (Req.IsFailure()) {
                return NullOpt;
            }
            FGameNetTestPurchaseRsp Rsp;
            Rsp.bSuccess = true;
            Rsp.RemainingGold = 10000 - Req.GetValue().BuyCount * 250;
            Rsp.DiscountRate = 0.9f;
            Rsp.ReceiptMessage = FString::Printf(TEXT("Item_%d_OK"), Req.GetValue().ShopItemId);
            Rsp.GrantedSerialNumbers = {701, 702};
            Rsp.BonusItem.ItemCode = FName(TEXT("BonusGem"));
            Rsp.BonusItem.Quantity = Req.GetValue().BuyCount;
            const TGameStatusResult<TArray<uint8>> EncodedRsp = NetService.GetSerializer()->SerializeStruct(Rsp);
            return EncodedRsp.IsSuccess() ? EncodedRsp.GetValue() : TOptional<TArray<uint8>>{};
        }
    );

    FNetHeartbeatConfig FastHeartbeat;
    FastHeartbeat.bEnabled = true;
    FastHeartbeat.IntervalSeconds = 2.0f;
    FastHeartbeat.TimeoutSeconds = 6.0f;
    FastHeartbeat.PingMessageId = 1;
    FastHeartbeat.PongMessageId = 2;
    NetService.SetHeartbeatConfig(FastHeartbeat);

    FNetReconnectConfig FastReconnect;
    FastReconnect.bEnableAutoReconnect = true;
    FastReconnect.BaseDelaySeconds = 1.0f;
    FastReconnect.MaxDelaySeconds = 4.0f;
    FastReconnect.MaxRetryAttempts = 2;
    FastReconnect.JitterRatio = 0.0f;
    NetService.SetReconnectConfig(FastReconnect, 7);

    TestTrue(TEXT("单机模式下使用统一 Connect 接口连接本地虚拟端点"), NetService.Connect(TEXT("mock://standalone")));
    TestTrue(TEXT("子系统报告已连接"), NetService.IsConnected());
    TestTrue(
        TEXT("消息总线收到已连接通知且状态枚举为 Connected"),
        !BusStateChanges.IsEmpty() && BusStateChanges.Last().ConnectionState == ENetConnectionState::Connected
    );

    int32 RemoteLogHits = 0;
    FGameRemoteLogEntry CapturedRemoteLog;
    MockTransport->RegisterMockHandler(
        NET_MSG_REMOTE_LOG,
        0,
        [&RemoteLogHits, &CapturedRemoteLog, &NetService](
            [[maybe_unused]] const FNetPacketHeader& Header,
            TConstArrayView<uint8> Payload
        ) -> TOptional<TArray<uint8>> {
            ++RemoteLogHits;
            const TGameStatusResult<FGameRemoteLogEntry> Deserialized =
                NetService.GetSerializer()->DeserializeStruct<FGameRemoteLogEntry>(Payload);
            if (Deserialized.IsSuccess()) {
                CapturedRemoteLog = Deserialized.GetValue();
            }
            return NullOpt;
        }
    );

    TestTrue(TEXT("默认开启远程日志自动转送"), NetService.IsRemoteLogForwardingEnabled());
    const int32 SentBeforeLog = MockTransport->GetSentPacketCount();

    const FGameRemoteLogEntry OutboundLogEntry(
        FName(TEXT("LogNetAudit")),
        static_cast<uint8>(ELogVerbosity::Error),
        TEXT("AuditRemoteAlert"),
        777.0,
        123
    );
    MessageBus.BroadcastMessage(TAG_Game_Log_Remote, OutboundLogEntry);

    TestEqual(TEXT("Mock 路由应捕获 1 次远程日志"), RemoteLogHits, 1);
    TestEqual(TEXT("发送上行包计数递增 1"), MockTransport->GetSentPacketCount(), SentBeforeLog + 1);
    TestEqual(TEXT("反序列化分类断言"), CapturedRemoteLog.Category, FName(TEXT("LogNetAudit")));
    TestEqual(TEXT("反序列化级别断言"), CapturedRemoteLog.GetVerbosity(), ELogVerbosity::Error);
    TestEqual(TEXT("反序列化正文断言"), CapturedRemoteLog.Message, FString(TEXT("AuditRemoteAlert")));
    TestEqual(TEXT("反序列化时间戳断言"), CapturedRemoteLog.Timestamp, 777.0);
    TestEqual(TEXT("反序列化帧号断言"), CapturedRemoteLog.FrameNumber, int64(123));

    NetService.SetRemoteLogForwardingEnabled(false);
    TestFalse(TEXT("远程日志转送已成功关闭"), NetService.IsRemoteLogForwardingEnabled());
    MessageBus.BroadcastMessage(TAG_Game_Log_Remote, OutboundLogEntry);
    TestEqual(TEXT("转送关闭后不应触发网络发送"), MockTransport->GetSentPacketCount(), SentBeforeLog + 1);
    TestEqual(TEXT("转送关闭后捕获次数不应增加"), RemoteLogHits, 1);
    NetService.SetRemoteLogForwardingEnabled(true);

    FGameNetTestPurchaseReq PurchaseReq;
    PurchaseReq.ShopItemId = 99;
    PurchaseReq.BuyCount = 2;
    PurchaseReq.CouponToken = TEXT("VIP90");

    ENetRequestResult RpcStatus = ENetRequestResult::SendFailed;
    FGameNetTestPurchaseRsp CapturedRsp;
    const TGameStatusResult<uint32> AssignedSeq = NetService.SendStructRequest<FGameNetTestPurchaseReq, FGameNetTestPurchaseRsp>(
        5001,
        5002,
        PurchaseReq,
        5.0f,
        [&RpcStatus, &CapturedRsp](ENetRequestResult Result, const FGameNetTestPurchaseRsp* ResponsePtr) {
            RpcStatus = Result;
            if (ResponsePtr != nullptr) {
                CapturedRsp = *ResponsePtr;
            }
        }
    );
    TestTrue(TEXT("成功分配非零异步请求序列号"), AssignedSeq.IsSuccess() && AssignedSeq.GetValue() > 0);

    NetService.Tick(0.05f);
    TestEqual(TEXT("单机无 Socket 下完整跑通异步购买请求"), RpcStatus, ENetRequestResult::Success);
    TestEqual(TEXT("扣款后金币余额计算精确为 9500"), CapturedRsp.RemainingGold, 9500);
    TestEqual(TEXT("收据文本精确匹配"), CapturedRsp.ReceiptMessage, FString(TEXT("Item_99_OK")));
    TestEqual(TEXT("赠送嵌套奖励数量精确匹配"), CapturedRsp.BonusItem.Quantity, 2);
    TestEqual(TEXT("下行报文同步桥接广播至消息总线"), BusInboundMessages.Num(), 1);

    MockTransport->SetEchoUnregisteredMessages(true);
    FGameNetOutboundMessage DecoupledOutbound;
    DecoupledOutbound.MessageId = 6100;
    DecoupledOutbound.SequenceId = 77;
    DecoupledOutbound.Payload = {0xAA, 0xBB, 0xCC};
    MessageBus.BroadcastMessage(TAG_Game_Net_Outbound, DecoupledOutbound);

    NetService.Tick(0.05f);
    TestEqual(TEXT("业务插件经由消息总线成功完成网络收发闭环"), BusInboundMessages.Num(), 2);
    TestEqual(TEXT("回环消息协议 ID 为 6100"), BusInboundMessages.Last().MessageId, 6100u);
    TestEqual(TEXT("回环消息负载一致"), BusInboundMessages.Last().Payload, DecoupledOutbound.Payload);
    MockTransport->SetEchoUnregisteredMessages(false);

    NetService.SetSerializer(MakeShared<FMemoryArchiveNetSerializer>());
    int32 BinaryPushHits = 0;
    const int64 StructHandlerId = NetService.RegisterStructHandler<FGameNetTestPurchaseReq>(
        6200,
        [&BinaryPushHits](const FNetPacketHeader&, const FGameNetTestPurchaseReq& PushedReq) {
            if (PushedReq.ShopItemId == 888) {
                ++BinaryPushHits;
            }
        }
    );
    MockTransport->SetEchoUnregisteredMessages(true);
    FGameNetTestPurchaseReq BinaryPushPayload;
    BinaryPushPayload.ShopItemId = 888;
    BinaryPushPayload.BuyCount = 1;
    TestTrue(TEXT("热切换为 UE 二进制 Archive 序列化器后发送成功"), NetService.SendStruct(6200, BinaryPushPayload).IsSuccess());
    NetService.Tick(0.05f);
    TestEqual(TEXT("强类型常驻监听器成功反解二进制负载"), BinaryPushHits, 1);
    TestTrue(TEXT("成功注销强类型监听器"), NetService.UnregisterHandler(6200, StructHandlerId).IsSuccess());
    MockTransport->SetEchoUnregisteredMessages(false);

    NetService.Tick(2.0f);
    NetService.Tick(0.05f);
    TestTrue(
        TEXT("心跳 Ping 自动发出并经 Pong 计算得到约 50ms RTT"),
        FMath::IsNearlyEqual(NetService.GetLastRttMilliseconds(), 50.0f, 1.0f)
    );

    MockTransport->SetAcceptConnections(false);
    MockTransport->SimulateRemoteDisconnect(TEXT("SimulatedGatewayRestart"));
    TestEqual(TEXT("意外断线后立即进入 Reconnecting 状态"), NetService.GetConnectionState(), ENetConnectionState::Reconnecting);
    TestEqual(TEXT("第 1 轮退避尝试计数为 1"), NetService.GetReconnectAttempt(), 1);

    NetService.Tick(1.1f);
    TestEqual(TEXT("第 1 次重连被拒后平滑进入第 2 轮 2.0s 退避"), NetService.GetReconnectAttempt(), 2);
    TestEqual(TEXT("状态继续维持 Reconnecting"), NetService.GetConnectionState(), ENetConnectionState::Reconnecting);

    MockTransport->SetAcceptConnections(true);
    NetService.Tick(2.1f);
    TestEqual(TEXT("第 2 轮退避到期重连恢复至 Connected"), NetService.GetConnectionState(), ENetConnectionState::Connected);
    TestEqual(TEXT("恢复连接后重试计数器自动清零"), NetService.GetReconnectAttempt(), 0);

    NetService.Disconnect(1000, TEXT("CleanExit"));
    TestEqual(TEXT("主动 Disconnect 后保持 Disconnected 不触发重连"), NetService.GetConnectionState(), ENetConnectionState::Disconnected);

    StateBusHandle.Unregister();
    InboundBusHandle.Unregister();
    GameInstance->Shutdown();
    return true;
}

#endif
