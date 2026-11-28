/**
 * @file GameNetServiceSubsystemTest.cpp
 * @brief 业务网络门面子系统（UGameNetServiceSubsystem）端到端编排、重入安全、取消令牌、故障恢复与协程测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Async/FCancellationTokenSource.h"
#include "Async/FGameCoroAwaiters.h"
#include "Async/TGameCoroTask.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Message/GameNetworkMessageTypes.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Remote/GameRemoteLogTypes.h"
#include "Serializer/MemoryArchiveNetSerializer.h"
#include "System/GameNetServiceSubsystem.h"
#include "Tests/GameNetCoreTestTypes.h"
#include "Transport/MockNetTransport.h"
#include "Types/NetErrorCodes.h"

namespace {

/**
 * @brief 验证 RequestStructAsync 协程门面的购买测试协程
 * @param NetService 网络服务门面子系统引用
 * @param ShopItemId 购买商品 ID
 * @param BuyCount 购买数量
 * @param TimeoutSeconds 超时秒数
 * @param Token 可选协同取消令牌
 * @param OutResult 输出网络请求结果枚举
 * @param OutRemainingGold 输出购买后剩余金币
 * @return 异步协程任务句柄
 */
TGameCoroTask<int32> RunCoroPurchaseRpc(
    UGameNetServiceSubsystem& NetService,
    int32 ShopItemId,
    int32 BuyCount,
    float TimeoutSeconds,
    FCancellationToken Token,
    ENetRequestResult& OutResult,
    int32& OutRemainingGold
) {
    FGameNetTestPurchaseReq Req;
    Req.ShopItemId = ShopItemId;
    Req.BuyCount = BuyCount;
    Req.CouponToken = TEXT("CORO");

    const TGameResult<FGameNetTestPurchaseRsp, ENetRequestResult> Result =
        co_await NetService.RequestStructAsync<FGameNetTestPurchaseReq, FGameNetTestPurchaseRsp>(
            5001,
            5002,
            Req,
            TimeoutSeconds,
            MoveTemp(Token)
        );

    if (Result.IsSuccess()) {
        OutResult = ENetRequestResult::Success;
        OutRemainingGold = Result.GetValue().RemainingGold;
    } else {
        OutResult = Result.GetError();
        OutRemainingGold = -1;
    }
    co_return OutRemainingGold;
}

/**
 * @brief 验证协程绑定宿主销毁后自动熔断自毁的测试协程
 * @param NetService 网络服务门面子系统引用
 * @param ContextObject 绑定宿主对象指针
 * @param OutPostAwaitExecuted 是否执行了挂起点之后的逻辑
 * @return 异步协程任务句柄
 */
TGameCoroTask<void> RunCoroGuardedPurchaseRpc(
    UGameNetServiceSubsystem& NetService,
    UObject* ContextObject,
    bool& OutPostAwaitExecuted
) {
    co_await Game::Coro::BindContext(ContextObject);
    FGameNetTestPurchaseReq Req;
    Req.ShopItemId = 77;
    Req.BuyCount = 1;
    Req.CouponToken = TEXT("GUARD");

    (void)co_await NetService.RequestStructAsync<FGameNetTestPurchaseReq, FGameNetTestPurchaseRsp>(
        5001,
        5002,
        Req,
        5.0f
    );

    OutPostAwaitExecuted = true;
    co_return;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetServiceSubsystemEndToEndTest,
    "Game.Net.Service.SubsystemEndToEnd",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证子系统 GetPtr 跨同级解析、重入 SendStruct 防写坏、RPC 全结果分支、协同取消、心跳超时与重连耗尽闭环
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
    TestEqual(
        TEXT("通过同级 UGameInstanceSubsystem（如 MessageBus）安全解析到同一个网络子系统"),
        UGameNetServiceSubsystem::GetPtr(&MessageBus),
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
        [&BusStateChanges](const FGameNetStateMessage& Msg) {
            BusStateChanges.Add(Msg);
        }
    );
    FGameplayMessageListenerHandle InboundBusHandle = MessageBus.RegisterListener<FGameNetInboundMessage>(
        TAG_Game_Net_Inbound,
        [&BusInboundMessages](const FGameNetInboundMessage& Msg) {
            BusInboundMessages.Add(Msg);
        }
    );

    ENetRequestResult DisconnectedRpcResult = ENetRequestResult::Success;
    const FGameNetTestPurchaseReq DummyReq;
    const TGameStatusResult<uint32> DisconnectedSendRes =
        NetService.SendStructRequest<FGameNetTestPurchaseReq, FGameNetTestPurchaseRsp>(
            5001,
            5002,
            DummyReq,
            5.0f,
            [&DisconnectedRpcResult](ENetRequestResult Result, const FGameNetTestPurchaseRsp*) {
                DisconnectedRpcResult = Result;
            }
        );
    TestTrue(TEXT("未连接时 SendStructRequest 返回失败态"), DisconnectedSendRes.IsFailure());
    TestEqual(TEXT("未连接错误归属为 Game::ModuleId::Net"), DisconnectedSendRes.GetError().ModuleId, Game::ModuleId::Net);
    TestEqual(TEXT("未连接回调收到 SendFailed"), DisconnectedRpcResult, ENetRequestResult::SendFailed);

    MockTransport->RegisterMockHandler(
        NET_MSG_HEARTBEAT_PING,
        NET_MSG_HEARTBEAT_PONG,
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
            if (Req.GetValue().CouponToken == TEXT("CORRUPT_RSP")) {
                const ANSICHAR BadBytes[] = "{broken_json";
                return TArray<uint8>(reinterpret_cast<const uint8*>(BadBytes), sizeof(BadBytes) - 1);
            }
            if (Req.GetValue().CouponToken == TEXT("DROP")) {
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
    NetService.SetHeartbeatConfig(FastHeartbeat);

    FNetReconnectConfig FastReconnect;
    FastReconnect.bEnableAutoReconnect = true;
    FastReconnect.BaseDelaySeconds = 1.0f;
    FastReconnect.MaxDelaySeconds = 4.0f;
    FastReconnect.MaxRetryAttempts = 2;
    FastReconnect.JitterRatio = 0.0f;
    NetService.SetReconnectConfig(FastReconnect, 7ULL);

    TestTrue(TEXT("单机模式下使用统一 Connect 接口连接本地虚拟端点"), NetService.Connect(TEXT("mock://standalone")));
    TestFalse(TEXT("已连接状态下重复 Connect 返回 false 且不破坏现有连接"), NetService.Connect(TEXT("mock://hijack")));
    TestTrue(TEXT("子系统保持已连接"), NetService.IsConnected());
    TestTrue(
        TEXT("消息总线收到已连接通知且状态枚举为 Connected"),
        !BusStateChanges.IsEmpty() && BusStateChanges.Last().ConnectionState == ENetConnectionState::Connected
    );

    int32 ReentrantInnerCapturedId = 0;
    int32 ReentrantOuterCapturedId = 0;
    MockTransport->RegisterMockHandler(
        5090,
        0,
        [&NetService, &ReentrantOuterCapturedId](const FNetPacketHeader&, TConstArrayView<uint8> OuterBytes) -> TOptional<TArray<uint8>> {
            FGameNetTestPurchaseReq InnerReq;
            InnerReq.ShopItemId = 4321;
            (void)NetService.SendStruct(5091, InnerReq);
            const TGameStatusResult<FGameNetTestPurchaseReq> OuterDecoded =
                NetService.GetSerializer()->DeserializeStruct<FGameNetTestPurchaseReq>(OuterBytes);
            if (OuterDecoded.IsSuccess()) {
                ReentrantOuterCapturedId = OuterDecoded.GetValue().ShopItemId;
            }
            return NullOpt;
        }
    );
    MockTransport->RegisterMockHandler(
        5091,
        0,
        [&NetService, &ReentrantInnerCapturedId](const FNetPacketHeader&, TConstArrayView<uint8> InnerBytes) -> TOptional<TArray<uint8>> {
            const TGameStatusResult<FGameNetTestPurchaseReq> InnerDecoded =
                NetService.GetSerializer()->DeserializeStruct<FGameNetTestPurchaseReq>(InnerBytes);
            if (InnerDecoded.IsSuccess()) {
                ReentrantInnerCapturedId = InnerDecoded.GetValue().ShopItemId;
            }
            return NullOpt;
        }
    );
    FGameNetTestPurchaseReq OuterSendReq;
    OuterSendReq.ShopItemId = 1234;
    TestTrue(TEXT("重入 SendStruct 发送成功"), NetService.SendStruct(5090, OuterSendReq).IsSuccess());
    TestEqual(TEXT("内层重入发包内容完整为 4321"), ReentrantInnerCapturedId, 4321);
    TestEqual(TEXT("外层正在读取的 SendScratchBuffer 未被重入覆写，保持 1234"), ReentrantOuterCapturedId, 1234);

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
    (void)MessageBus.BroadcastMessage(TAG_Game_Log_Remote, OutboundLogEntry);
    TestEqual(TEXT("Mock 路由应捕获 1 次远程日志"), RemoteLogHits, 1);
    TestEqual(TEXT("发送上行包计数递增 1"), MockTransport->GetSentPacketCount(), SentBeforeLog + 1);
    TestEqual(TEXT("反序列化分类断言"), CapturedRemoteLog.Category, FName(TEXT("LogNetAudit")));
    TestEqual(TEXT("反序列化正文断言"), CapturedRemoteLog.Message, FString(TEXT("AuditRemoteAlert")));

    NetService.SetRemoteLogForwardingEnabled(false);
    (void)MessageBus.BroadcastMessage(TAG_Game_Log_Remote, OutboundLogEntry);
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
    TestEqual(TEXT("下行报文同步桥接广播至消息总线"), BusInboundMessages.Num(), 1);

    FGameNetTestPurchaseReq CorruptReq = PurchaseReq;
    CorruptReq.CouponToken = TEXT("CORRUPT_RSP");
    ENetRequestResult DeserializeErrStatus = ENetRequestResult::Success;
    (void)NetService.SendStructRequest<FGameNetTestPurchaseReq, FGameNetTestPurchaseRsp>(
        5001,
        5002,
        CorruptReq,
        5.0f,
        [&DeserializeErrStatus](ENetRequestResult Result, const FGameNetTestPurchaseRsp*) {
            DeserializeErrStatus = Result;
        }
    );
    NetService.Tick(0.05f);
    TestEqual(TEXT("损坏响应负载精确触发 DeserializeFailed"), DeserializeErrStatus, ENetRequestResult::DeserializeFailed);

    FGameNetTestPurchaseReq DropReq = PurchaseReq;
    DropReq.CouponToken = TEXT("DROP");
    FCancellationTokenSource CancelSource;
    ENetRequestResult CancelledRpcStatus = ENetRequestResult::Success;
    (void)NetService.SendStructRequest<FGameNetTestPurchaseReq, FGameNetTestPurchaseRsp>(
        5001,
        5002,
        DropReq,
        5.0f,
        [&CancelledRpcStatus](ENetRequestResult Result, const FGameNetTestPurchaseRsp*) {
            CancelledRpcStatus = Result;
        },
        CancelSource.GetToken()
    );
    CancelSource.Cancel();
    TestEqual(TEXT("FCancellationToken 触发取消后请求立即结算为 Cancelled"), CancelledRpcStatus, ENetRequestResult::Cancelled);

    FCancellationTokenSource MidSendCancelSource;
    MockTransport->RegisterMockHandler(
        5092,
        5093,
        [&MidSendCancelSource](const FNetPacketHeader&, TConstArrayView<uint8>) -> TOptional<TArray<uint8>> {
            MidSendCancelSource.Cancel();
            return NullOpt;
        }
    );
    ENetRequestResult MidSendCancelResult = ENetRequestResult::Success;
    int32 MidSendCallbackCount = 0;
    const TGameStatusResult<uint32> MidSendRes = NetService.SendRawRequest(
        5092,
        5093,
        {},
        0.2f,
        [&MidSendCancelResult, &MidSendCallbackCount](ENetRequestResult Result, const FNetPacketHeader&, TConstArrayView<uint8>) {
            MidSendCancelResult = Result;
            ++MidSendCallbackCount;
        },
        MidSendCancelSource.GetToken()
    );
    TestTrue(TEXT("Send 期间令牌被取消时 SendRawRequest 返回 Failure"), MidSendRes.IsFailure());
    TestEqual(TEXT("错误码为 RequestCancelled"), MidSendRes.GetError().ErrorCode, Game::NetErrorCodes::RequestCancelled);
    TestEqual(TEXT("同步取消精确触发 1 次 Cancelled 回调"), MidSendCallbackCount, 1);
    TestEqual(TEXT("回调状态为 Cancelled"), MidSendCancelResult, ENetRequestResult::Cancelled);
    NetService.Tick(0.5f);
    TestEqual(TEXT("已同步取消的请求不再残留触发 TimedOut 二次回调"), MidSendCallbackCount, 1);

    ENetRequestResult TimedOutRpcStatus = ENetRequestResult::Success;
    (void)NetService.SendStructRequest<FGameNetTestPurchaseReq, FGameNetTestPurchaseRsp>(
        5001,
        5002,
        DropReq,
        0.5f,
        [&TimedOutRpcStatus](ENetRequestResult Result, const FGameNetTestPurchaseRsp*) {
            TimedOutRpcStatus = Result;
        }
    );
    NetService.Tick(0.6f);
    TestEqual(TEXT("无响应请求超期触发 TimedOut"), TimedOutRpcStatus, ENetRequestResult::TimedOut);

    MockTransport->SetEchoUnregisteredMessages(true);
    FGameNetOutboundMessage DecoupledOutbound(6100, 77, TArray<uint8>{0xAA, 0xBB, 0xCC});
    (void)MessageBus.BroadcastMessage(TAG_Game_Net_Outbound, DecoupledOutbound);
    NetService.Tick(0.05f);
    TestEqual(TEXT("回环消息协议 ID 为 6100"), BusInboundMessages.Last().MessageId, 6100u);
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
    TestTrue(TEXT("热切换为 UE 二进制 Archive 序列化器后发送成功"), NetService.SendStruct(6200, BinaryPushPayload).IsSuccess());
    NetService.Tick(0.05f);
    TestEqual(TEXT("强类型常驻监听器成功反解二进制负载"), BinaryPushHits, 1);
    TestTrue(TEXT("成功注销强类型监听器"), NetService.UnregisterHandler(6200, StructHandlerId));
    MockTransport->SetEchoUnregisteredMessages(false);

    NetService.Tick(2.0f);
    NetService.Tick(0.05f);
    TestTrue(
        TEXT("心跳 Ping 自动发出并经 Pong 计算得到约 50ms RTT"),
        FMath::IsNearlyEqual(NetService.GetLastRttMilliseconds(), 50.0f, 1.0f)
    );
    TestTrue(
        TEXT("平滑平均 RTT 同步更新"),
        FMath::IsNearlyEqual(NetService.GetAverageRttMilliseconds(), 50.0f, 1.0f)
    );

    MockTransport->UnregisterMockHandler(NET_MSG_HEARTBEAT_PING);
    MockTransport->SetAcceptConnections(false);
    NetService.Tick(6.1f);
    TestEqual(TEXT("心跳静默超时主动断开后自动进入 Reconnecting 状态"), NetService.GetConnectionState(), ENetConnectionState::Reconnecting);
    TestEqual(TEXT("第 1 轮退避尝试计数为 1"), NetService.GetReconnectAttempt(), 1);

    NetService.Tick(1.1f);
    TestEqual(TEXT("第 1 次重连被拒后进入第 2 轮退避"), NetService.GetReconnectAttempt(), 2);

    NetService.Tick(2.1f);
    TestEqual(TEXT("重连次数耗尽后最终回落 Disconnected 状态"), NetService.GetConnectionState(), ENetConnectionState::Disconnected);
    TestEqual(TEXT("总线收到最终 Disconnected 广播"), BusStateChanges.Last().ConnectionState, ENetConnectionState::Disconnected);

    MockTransport->SetAcceptConnections(true);
    TestTrue(TEXT("耗尽后允许手动重新 Connect"), NetService.Connect(TEXT("mock://recovered")));

    MockTransport->RegisterMockHandler(
        5094,
        5095,
        [&MockTransport](const FNetPacketHeader&, TConstArrayView<uint8>) -> TOptional<TArray<uint8>> {
            MockTransport->SimulateRemoteDisconnect(TEXT("DropDuringSend"));
            return NullOpt;
        }
    );
    ENetRequestResult MidSendDropResult = ENetRequestResult::Success;
    int32 MidSendDropHits = 0;
    const TGameStatusResult<uint32> MidSendDropRes = NetService.SendRawRequest(
        5094,
        5095,
        {},
        0.2f,
        [&MidSendDropResult, &MidSendDropHits](ENetRequestResult Result, const FNetPacketHeader&, TConstArrayView<uint8>) {
            MidSendDropResult = Result;
            ++MidSendDropHits;
        }
    );
    TestTrue(TEXT("发包期触发同步断线时 SendRawRequest 返回 Failure"), MidSendDropRes.IsFailure());
    TestEqual(TEXT("发包期断线错误码为 NotConnected"), MidSendDropRes.GetError().ErrorCode, Game::NetErrorCodes::NotConnected);
    TestEqual(TEXT("发包期断线回调立即收到 ConnectionClosed"), MidSendDropResult, ENetRequestResult::ConnectionClosed);
    TestEqual(TEXT("发包期断线回调仅触发 1 次"), MidSendDropHits, 1);
    NetService.Disconnect(1000, TEXT("CancelReconnectAfterDrop"));
    NetService.Tick(0.5f);
    TestEqual(TEXT("发包期断线未残留僵尸超时回调"), MidSendDropHits, 1);

    TestTrue(TEXT("重连以验证 Disconnect 回调内重入 Connect 安全"), NetService.Connect(TEXT("mock://before_reentrant_disc")));
    bool bReconnectedInsideDisconnect = false;
    (void)NetService.SendStructRequest<FGameNetTestPurchaseReq, FGameNetTestPurchaseRsp>(
        5001,
        5002,
        DropReq,
        5.0f,
        [&NetService, &bReconnectedInsideDisconnect](ENetRequestResult Result, const FGameNetTestPurchaseRsp*) {
            if (Result == ENetRequestResult::ConnectionClosed && !bReconnectedInsideDisconnect) {
                bReconnectedInsideDisconnect = NetService.Connect(TEXT("mock://fallback"));
            }
        }
    );
    NetService.Disconnect(1000, TEXT("TriggerReentrantFallback"));
    TestTrue(TEXT("Disconnect 触发的挂起回调内重入 Connect 成功"), bReconnectedInsideDisconnect);
    TestTrue(TEXT("重入建立的备用连接未被外层 Disconnect 误杀"), NetService.IsConnected());
    TestEqual(
        TEXT("备用连接端点完整保留为 mock://fallback"),
        MockTransport->GetConnectedEndpoint(),
        FString(TEXT("mock://fallback"))
    );

    NetService.Disconnect(1000, TEXT("CleanExit"));
    TestEqual(TEXT("主动 Disconnect 后保持 Disconnected 不触发重连"), NetService.GetConnectionState(), ENetConnectionState::Disconnected);

    StateBusHandle.Unregister();
    InboundBusHandle.Unregister();
    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetServiceSubsystemCoroTest,
    "Game.Net.Service.SubsystemCoro",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 RequestStructAsync C++20 协程门面成功、取消、超时与宿主销毁自毁熔断全路径
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetServiceSubsystemCoroTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameNetServiceCoroTestWorld")));

    UGameNetServiceSubsystem& NetService = UGameNetServiceSubsystem::Get(GameInstance);
    const TSharedRef<FMockNetTransport> MockTransport = MakeShared<FMockNetTransport>();
    NetService.SetTransport(MockTransport);

    bool bDropReplies = false;
    MockTransport->RegisterMockHandler(
        5001,
        5002,
        [&NetService, &bDropReplies](const FNetPacketHeader&, TConstArrayView<uint8> ReqBytes) -> TOptional<TArray<uint8>> {
            if (bDropReplies) {
                return NullOpt;
            }
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
            const TGameStatusResult<TArray<uint8>> EncodedRsp = NetService.GetSerializer()->SerializeStruct(Rsp);
            return EncodedRsp.IsSuccess() ? EncodedRsp.GetValue() : TOptional<TArray<uint8>>{};
        }
    );

    ENetRequestResult DisconnectedCoroStatus = ENetRequestResult::Success;
    int32 DisconnectedCoroGold = 0;
    const TGameCoroTask<int32> DisconnectedCoroTask = RunCoroPurchaseRpc(
        NetService,
        100,
        1,
        5.0f,
        FCancellationToken::None(),
        DisconnectedCoroStatus,
        DisconnectedCoroGold
    );
    TestTrue(TEXT("断线态发起协程 RPC 同步恢复完成"), DisconnectedCoroTask.IsDone());
    TestEqual(TEXT("断线态协程 RPC 精确返回 SendFailed"), DisconnectedCoroStatus, ENetRequestResult::SendFailed);

    TestTrue(TEXT("连接 Mock 端点"), NetService.Connect(TEXT("mock://coro_test")));

    FCancellationTokenSource PreCancelledSource;
    PreCancelledSource.Cancel();
    ENetRequestResult PreCancelledCoroStatus = ENetRequestResult::Success;
    int32 PreCancelledCoroGold = 0;
    const TGameCoroTask<int32> PreCancelledCoroTask = RunCoroPurchaseRpc(
        NetService,
        100,
        1,
        5.0f,
        PreCancelledSource.GetToken(),
        PreCancelledCoroStatus,
        PreCancelledCoroGold
    );
    TestTrue(TEXT("预取消令牌发起协程 RPC 同步恢复完成"), PreCancelledCoroTask.IsDone());
    TestEqual(TEXT("预取消令牌协程 RPC 精确返回 Cancelled"), PreCancelledCoroStatus, ENetRequestResult::Cancelled);

    ENetRequestResult CoroRpcStatus = ENetRequestResult::SendFailed;
    int32 CoroRemainingGold = 0;
    TGameCoroTask<int32> CoroTask = RunCoroPurchaseRpc(
        NetService,
        101,
        3,
        5.0f,
        FCancellationToken::None(),
        CoroRpcStatus,
        CoroRemainingGold
    );
    TestFalse(TEXT("协程挂起等待网络回包未就绪"), CoroTask.IsDone());
    NetService.Tick(0.05f);
    TestTrue(TEXT("网络回包后协程正常恢复并完成"), CoroTask.IsDone());
    TestTrue(TEXT("协程执行状态为成功"), CoroTask.GetResult().IsSuccess());
    TestEqual(TEXT("协程返回值正确"), CoroTask.GetResult().GetValue(), 9250);
    TestEqual(TEXT("协程提取回包状态成功"), CoroRpcStatus, ENetRequestResult::Success);

    bDropReplies = true;
    FCancellationTokenSource CoroCancelSource;
    ENetRequestResult CancelledStatus = ENetRequestResult::Success;
    int32 CancelledGold = 0;
    TGameCoroTask<int32> CancelTask = RunCoroPurchaseRpc(
        NetService,
        102,
        1,
        5.0f,
        CoroCancelSource.GetToken(),
        CancelledStatus,
        CancelledGold
    );
    TestFalse(TEXT("待取消协程处于挂起态"), CancelTask.IsDone());
    CoroCancelSource.Cancel();
    TestTrue(TEXT("取消令牌触发后协程立即恢复结束"), CancelTask.IsDone());
    TestEqual(TEXT("协程收到精确的 Cancelled 错误枚举"), CancelledStatus, ENetRequestResult::Cancelled);

    ENetRequestResult TimeoutStatus = ENetRequestResult::Success;
    int32 TimeoutGold = 0;
    TGameCoroTask<int32> TimeoutTask = RunCoroPurchaseRpc(
        NetService,
        103,
        1,
        0.5f,
        FCancellationToken::None(),
        TimeoutStatus,
        TimeoutGold
    );
    NetService.Tick(0.6f);
    TestTrue(TEXT("超时后协程恢复完成"), TimeoutTask.IsDone());
    TestEqual(TEXT("协程收到精确的 TimedOut 错误枚举"), TimeoutStatus, ENetRequestResult::TimedOut);
    bDropReplies = false;

    bool bCoroPostAwaitRan = false;
    UGameInstance* TempCoroOwner = NewObject<UGameInstance>();
    TGameCoroTask<void> GuardedTask = RunCoroGuardedPurchaseRpc(
        NetService,
        TempCoroOwner,
        bCoroPostAwaitRan
    );
    TestFalse(TEXT("守卫协程处于挂起状态"), GuardedTask.IsDone());
    TempCoroOwner->MarkAsGarbage();
    NetService.Tick(0.05f);
    TestFalse(TEXT("宿主销毁后挂起点后的代码绝不执行"), bCoroPostAwaitRan);
    TestTrue(TEXT("宿主销毁后协程处于已取消终态"), GuardedTask.IsCancelled());

    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetServiceHotSwapInCallbackTest,
    "Game.Net.Service.HotSwapInCallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证收包回调内热切换传输时旧驱动在 Tick 调用栈内保持存活，不发生 UAF
 * @param Parameters 自动化测试参数
 * @return 热切换完成且当前驱动为新实例返回 true
 */
bool FGameNetServiceHotSwapInCallbackTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameNetServiceHotSwapTestWorld")));

    UGameNetServiceSubsystem& NetService = UGameNetServiceSubsystem::Get(GameInstance);
    const TSharedRef<FMockNetTransport> TransportA = MakeShared<FMockNetTransport>();
    const TSharedRef<FMockNetTransport> TransportB = MakeShared<FMockNetTransport>();

    NetService.SetTransport(TransportA);
    TestTrue(TEXT("应成功连接传输 A"), NetService.Connect(TEXT("mock://hot-swap-a")));

    bool bSwappedInCallback = false;
    NetService.RegisterRawHandler(
        6001,
        [&NetService, TransportB, &bSwappedInCallback](
            [[maybe_unused]] const FNetPacketHeader& Header,
            [[maybe_unused]] TConstArrayView<uint8> Payload
        ) {
            NetService.SetTransport(TransportB);
            bSwappedInCallback = true;
        }
    );

    TestTrue(TEXT("应成功注入服务端推送"), TransportA->SimulateServerPush(6001, {}, 0));
    NetService.Tick(0.05f);

    TestTrue(TEXT("收包回调内应完成热切换"), bSwappedInCallback);
    TestEqual(
        TEXT("热切换后当前传输应为 B"),
        NetService.GetTransport().Get(),
        static_cast<INetTransport*>(&TransportB.Get())
    );

    GameInstance->Shutdown();
    return true;
}

#endif
