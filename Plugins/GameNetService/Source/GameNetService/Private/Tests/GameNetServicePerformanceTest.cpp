/**
 * @file GameNetServicePerformanceTest.cpp
 * @brief GameNetService 模块多轮统计微基准性能测试套件（重连退避 / 心跳平滑 RTT / 异步请求追踪 / 分片重组 / 多播派发 / WebSocket 零拷贝收包 / 消息总线无监听短路 / 子系统 RPC）
 *
 * 遵循通用代码规范与 UE5 性能基准规范：多轮预热与采样（>= 5 轮），统计中位数、均值、Min/Max 与变异系数 (CV%)。
 */

#include "CoreMinimal.h"

#if WITH_AUTOMATION_TESTS

#include "Bridge/NetMessageBusBridge.h"
#include "Codec/NetPacketCodec.h"
#include "Dispatch/NetDispatchTypes.h"
#include "Dispatch/NetMessageDispatcher.h"
#include "Dispatch/NetPendingRequestTracker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"
#include "IWebSocket.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Policy/NetHeartbeatKeeper.h"
#include "Policy/NetPolicyTypes.h"
#include "Policy/NetReconnectPolicy.h"
#include "System/GameNetServiceSubsystem.h"
#include "Tests/GameNetCoreTestTypes.h"
#include "Transport/MockNetTransport.h"
#include "Transport/WebSocketFragmentAssembler.h"
#include "Transport/WebSocketNetTransport.h"

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 5;

/**
 * @brief 多轮基准测试统计指标快照
 */
struct FNetServiceBenchmarkMetrics {
    double MedianNs{0.0};
    double MeanNs{0.0};
    double CvPercent{0.0};
    double MinNs{0.0};
    double MaxNs{0.0};
    double OpsPerSec{0.0};
};

/**
 * @brief 执行多轮基准测试，消除预热偏差并统计中位数、均值、极值与离散度 (CV%)
 * @tparam TFunc 待测闭包类型
 * @param WarmupRounds 预热轮数
 * @param SampleRounds 正式采样轮数（>= 5）
 * @param IterationsPerRound 每轮迭代次数
 * @param Body 待执行测量闭包
 * @return 统计度量结果
 */
template <typename TFunc>
FNetServiceBenchmarkMetrics RunServiceBenchmark(
    int32 WarmupRounds,
    int32 SampleRounds,
    int32 IterationsPerRound,
    TFunc&& Body
) {
    for (int32 Round = 0; Round < WarmupRounds; ++Round) {
        Body(IterationsPerRound);
    }

    TArray<double> SamplesNs;
    SamplesNs.Reserve(SampleRounds);

    double SumNs = 0.0;
    for (int32 Round = 0; Round < SampleRounds; ++Round) {
        const uint64 StartCycles = FPlatformTime::Cycles64();
        Body(IterationsPerRound);
        const double ElapsedSec = FMath::Max(FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartCycles), 1.0e-9);
        const double NsPerOp = (ElapsedSec * 1.0e9) / static_cast<double>(IterationsPerRound);
        SamplesNs.Add(NsPerOp);
        SumNs += NsPerOp;
    }

    SamplesNs.Sort();
    const double MedianNs = SamplesNs[SampleRounds / 2];
    const double MeanNs = SumNs / static_cast<double>(SampleRounds);

    double VarianceSum = 0.0;
    for (const double Sample : SamplesNs) {
        const double Diff = Sample - MeanNs;
        VarianceSum += Diff * Diff;
    }
    const double StdDev = FMath::Sqrt(VarianceSum / static_cast<double>(SampleRounds));

    FNetServiceBenchmarkMetrics Metrics;
    Metrics.MedianNs = MedianNs;
    Metrics.MeanNs = MeanNs;
    Metrics.CvPercent = (MeanNs > 0.0) ? ((StdDev / MeanNs) * 100.0) : 0.0;
    Metrics.MinNs = SamplesNs[0];
    Metrics.MaxNs = SamplesNs.Last();
    Metrics.OpsPerSec = (MedianNs > 0.0) ? (1.0e9 / MedianNs) : 0.0;
    return Metrics;
}

/**
 * @brief 用于 WebSocket 收包链路基准测试的轻量零开销桩实现
 */
class FPerfFakeWebSocket final : public IWebSocket {
public:
    /**
     * @brief 空实现连接触发
     */
    void Connect() override {}

    /**
     * @brief 标记连接已断开
     * @param Code 关闭码
     * @param Reason 关闭原因
     */
    void Close([[maybe_unused]] int32 Code, [[maybe_unused]] const FString& Reason) override {
        bConnected = false;
    }

    /**
     * @brief 查询模拟连接状态
     * @return 已连接返回 true
     */
    bool IsConnected() override {
        return bConnected;
    }

    /**
     * @brief 空实现文本发送
     * @param Data 文本内容
     */
    void Send([[maybe_unused]] const FString& Data) override {}

    /**
     * @brief 空实现二进制发送
     * @param Data 字节指针
     * @param Size 字节长度
     * @param bIsBinary 是否二进制帧
     */
    void Send([[maybe_unused]] const void* Data, [[maybe_unused]] SIZE_T Size, [[maybe_unused]] bool bIsBinary) override {}

    /**
     * @brief 空实现文本内存限制设置
     * @param MessageSizeLimit 字节上限
     */
    void SetTextMessageMemoryLimit([[maybe_unused]] uint64 MessageSizeLimit) override {}

    /**
     * @brief 获取连接成功事件引用
     * @return 连接成功委托引用
     */
    FWebSocketConnectedEvent& OnConnected() override {
        return ConnectedEvent;
    }

    /**
     * @brief 获取连接错误事件引用
     * @return 连接错误委托引用
     */
    FWebSocketConnectionErrorEvent& OnConnectionError() override {
        return ConnectionErrorEvent;
    }

    /**
     * @brief 获取连接关闭事件引用
     * @return 连接关闭委托引用
     */
    FWebSocketClosedEvent& OnClosed() override {
        return ClosedEvent;
    }

    /**
     * @brief 获取文本消息事件引用
     * @return 文本消息委托引用
     */
    FWebSocketMessageEvent& OnMessage() override {
        return MessageEvent;
    }

    /**
     * @brief 获取二进制消息事件引用
     * @return 二进制消息委托引用
     */
    FWebSocketBinaryMessageEvent& OnBinaryMessage() override {
        return BinaryMessageEvent;
    }

    /**
     * @brief 获取原始消息事件引用
     * @return 原始消息委托引用
     */
    FWebSocketRawMessageEvent& OnRawMessage() override {
        return RawMessageEvent;
    }

    /**
     * @brief 获取消息发送完成事件引用
     * @return 发送完成委托引用
     */
    FWebSocketMessageSentEvent& OnMessageSent() override {
        return MessageSentEvent;
    }

    /**
     * @brief 模拟底层连接建立事件触发
     */
    void PushConnected() {
        bConnected = true;
        ConnectedEvent.Broadcast();
    }

    /**
     * @brief 模拟底层 IO 回调推送一帧二进制消息
     * @param Data 数据指针
     * @param Size 字节长度
     * @param bLast 是否末分片
     */
    void PushBinary(const uint8* Data, SIZE_T Size, bool bLast) {
        BinaryMessageEvent.Broadcast(Data, Size, bLast);
    }

private:
    bool bConnected{false};
    FWebSocketConnectedEvent ConnectedEvent;
    FWebSocketConnectionErrorEvent ConnectionErrorEvent;
    FWebSocketClosedEvent ClosedEvent;
    FWebSocketMessageEvent MessageEvent;
    FWebSocketBinaryMessageEvent BinaryMessageEvent;
    FWebSocketRawMessageEvent RawMessageEvent;
    FWebSocketMessageSentEvent MessageSentEvent;
};

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNetReconnectPolicyPerfTest,
    "Game.Net.Service.Perf.ReconnectPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 多轮基准测试指数退避重连调度与计时推进性能
 * @param Parameters 自动化测试参数
 * @return 测试是否通过性能预算
 */
bool FNetReconnectPolicyPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetReconnectConfig Config;
    Config.bEnableAutoReconnect = true;
    Config.BaseDelaySeconds = 0.5f;
    Config.MaxDelaySeconds = 16.0f;
    Config.MaxRetryAttempts = 10;
    Config.JitterRatio = 0.2f;

    FNetReconnectPolicy Policy(Config, 42ULL);
    int32 ScheduledCount = 0;

    constexpr int32 IterationsPerRound = 20000;
    const FNetServiceBenchmarkMetrics Metrics = RunServiceBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&Policy, &ScheduledCount](int32 Count) {
            for (int32 Index = 0; Index < Count; ++Index) {
                const TOptional<float> Delay = Policy.ScheduleNextAttempt();
                if (Delay.IsSet()) {
                    ++ScheduledCount;
                    (void)Policy.AdvanceTimer(*Delay);
                } else {
                    Policy.Reset();
                }
            }
        }
    );

    TestTrue(TEXT("成功调度次数大于 0"), ScheduledCount > 0);
    AddInfo(FString::Printf(
        TEXT("【基准】ReconnectPolicy: Median=%.2f ns, Mean=%.2f ns, CV=%.1f%%, 吞吐=%.2f Mops/s"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));

    constexpr double BudgetNsPerOp = 200.0;
    TestTrue(
        FString::Printf(TEXT("重连退避计算中位耗时应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, Metrics.MedianNs),
        Metrics.MedianNs < BudgetNsPerOp
    );
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNetHeartbeatKeeperPerfTest,
    "Game.Net.Service.Perf.HeartbeatKeeper",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 多轮基准测试心跳状态机推进与 FMovingAverageSampler 平滑 RTT 采样性能
 * @param Parameters 自动化测试参数
 * @return 测试是否通过性能预算
 */
bool FNetHeartbeatKeeperPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetHeartbeatConfig Config;
    Config.bEnabled = true;
    Config.IntervalSeconds = 5.0f;
    Config.TimeoutSeconds = 15.0f;
    Config.PingMessageId = 101;
    Config.PongMessageId = 102;

    FNetHeartbeatKeeper Keeper(Config);
    double SimulatedClock = 500.0;

    constexpr int32 IterationsPerRound = 40000;
    const FNetServiceBenchmarkMetrics Metrics = RunServiceBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&Keeper, &SimulatedClock](int32 Count) {
            for (int32 Index = 0; Index < Count; ++Index) {
                SimulatedClock += 0.016;
                (void)Keeper.Tick(0.016f);
                Keeper.RecordPingSent(static_cast<uint32>(Index + 1), SimulatedClock);
                Keeper.NotifyInboundPacket(102, static_cast<uint32>(Index + 1), SimulatedClock + 0.022);
            }
        }
    );

    TestTrue(TEXT("最终成功测算最新与平滑 RTT"), Keeper.GetLastRttMilliseconds() > 0.0f && Keeper.GetAverageRttMilliseconds() > 0.0f);
    AddInfo(FString::Printf(
        TEXT("【基准】HeartbeatKeeper: Median=%.2f ns, Mean=%.2f ns, CV=%.1f%%, 吞吐=%.2f Mops/s"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));

    constexpr double BudgetNsPerOp = 120.0;
    TestTrue(
        FString::Printf(TEXT("心跳与平滑RTT更新中位耗时应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, Metrics.MedianNs),
        Metrics.MedianNs < BudgetNsPerOp
    );
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNetPendingRequestTrackerPerfTest,
    "Game.Net.Service.Perf.RequestTracker",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 多轮基准测试异步请求序列号分配、登记与单次查找配对结算性能
 * @param Parameters 自动化测试参数
 * @return 测试是否通过性能预算
 */
bool FNetPendingRequestTrackerPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetPendingRequestTracker Tracker;
    int32 SettledCount = 0;
    const uint8 DummyPayload[] = {1, 2, 3, 4};

    constexpr int32 IterationsPerRound = 20000;
    const FNetServiceBenchmarkMetrics Metrics = RunServiceBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&Tracker, &SettledCount, &DummyPayload](int32 Count) {
            for (int32 Index = 0; Index < Count; ++Index) {
                const uint32 Seq = Tracker.AllocateSequenceId();
                Tracker.RegisterPendingRequest(
                    Seq,
                    200,
                    5.0f,
                    [&SettledCount](ENetRequestResult Res, const FNetPacketHeader&, TConstArrayView<uint8>) {
                        if (Res == ENetRequestResult::Success) {
                            ++SettledCount;
                        }
                    }
                );
                FNetPacketHeader RspHeader;
                RspHeader.MessageId = 200;
                RspHeader.SequenceId = Seq;
                (void)Tracker.TryCompleteResponse(RspHeader, DummyPayload);
            }
        }
    );

    TestEqual(TEXT("结算后挂起数为 0"), Tracker.GetPendingCount(), 0);
    TestTrue(TEXT("成功结算数量大于 0"), SettledCount > 0);
    AddInfo(FString::Printf(
        TEXT("【基准】PendingRequestTracker: Median=%.2f ns, Mean=%.2f ns, CV=%.1f%%, 吞吐=%.2f Kops/s"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e3
    ));

    constexpr double BudgetNsPerOp = 800.0;
    TestTrue(
        FString::Printf(TEXT("异步请求配对中位耗时应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, Metrics.MedianNs),
        Metrics.MedianNs < BudgetNsPerOp
    );
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWebSocketFragmentAssemblerPerfTest,
    "Game.Net.Service.Perf.FragmentAssembler",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 多轮基准测试 WebSocket 双分片零外拷贝只读视图重组吞吐性能
 * @param Parameters 自动化测试参数
 * @return 测试是否通过性能预算
 */
bool FWebSocketFragmentAssemblerPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FWebSocketFragmentAssembler Assembler(65536);

    uint8 ChunkA[256];
    uint8 ChunkB[256];
    FMemory::Memset(ChunkA, 0xAA, sizeof(ChunkA));
    FMemory::Memset(ChunkB, 0xBB, sizeof(ChunkB));

    TConstArrayView<uint8> AssembledView;
    int32 CompleteMessageCount = 0;

    constexpr int32 IterationsPerRound = 20000;
    const FNetServiceBenchmarkMetrics Metrics = RunServiceBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&Assembler, &ChunkA, &ChunkB, &AssembledView, &CompleteMessageCount](int32 Count) {
            for (int32 Index = 0; Index < Count; ++Index) {
                const EWebSocketFragmentStatus StatusA = Assembler.AppendFragment(ChunkA, false, AssembledView);
                const EWebSocketFragmentStatus StatusB = Assembler.AppendFragment(ChunkB, true, AssembledView);
                if (StatusA == EWebSocketFragmentStatus::Incomplete && StatusB == EWebSocketFragmentStatus::MessageReady) {
                    ++CompleteMessageCount;
                }
            }
        }
    );

    TestTrue(TEXT("拼装成包总数大于 0"), CompleteMessageCount > 0);
    TestEqual(TEXT("单条拼装视图长度正确"), AssembledView.Num(), static_cast<int32>(sizeof(ChunkA) + sizeof(ChunkB)));
    AddInfo(FString::Printf(
        TEXT("【基准】FragmentAssembler(2-Frag View): Median=%.2f ns, Mean=%.2f ns, CV=%.1f%%, 吞吐=%.2f Kops/s"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e3
    ));

    constexpr double BudgetNsPerOp = 1500.0;
    TestTrue(
        FString::Printf(TEXT("两分片零外拷贝拼装中位耗时应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, Metrics.MedianNs),
        Metrics.MedianNs < BudgetNsPerOp
    );
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWebSocketRxPipelinePerfTest,
    "Game.Net.Service.Perf.WebSocketRxZeroCopy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 多轮基准测试 WebSocket 有界无锁环入队与单分片零拷贝快路径收包派发性能
 * @param Parameters 自动化测试参数
 * @return 测试是否通过性能预算
 */
bool FWebSocketRxPipelinePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSharedPtr<FPerfFakeWebSocket> FakeSocket;
    FWebSocketNetTransport Transport(
        4096,
        [&FakeSocket](const FString&, const FString&, const TMap<FString, FString>&) -> TSharedPtr<IWebSocket> {
            FakeSocket = MakeShared<FPerfFakeWebSocket>();
            return FakeSocket;
        }
    );

    TestTrue(TEXT("连接性能桩 WebSocket"), Transport.Connect(TEXT("wss://perf")).IsSuccess());
    FakeSocket->PushConnected();
    Transport.Tick(0.016f);

    int32 ReceivedCount = 0;
    Transport.OnPacketReceived().AddLambda([&ReceivedCount](const FNetPacketHeader&, TConstArrayView<uint8>) {
        ++ReceivedCount;
    });

    FNetPacketHeader Header;
    Header.MessageId = 9001;
    const uint8 Payload[32] = {1, 2, 3, 4};
    TArray<uint8> WireFrame;
    FNetPacketCodec::EncodeFrameTo(Header, Payload, WireFrame);

    constexpr int32 BatchSize = 256;
    constexpr int32 BatchesPerRound = 20;
    constexpr int32 IterationsPerRound = BatchSize * BatchesPerRound;

    const FNetServiceBenchmarkMetrics Metrics = RunServiceBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&FakeSocket, &Transport, &WireFrame](int32) {
            for (int32 Batch = 0; Batch < BatchesPerRound; ++Batch) {
                for (int32 Idx = 0; Idx < BatchSize; ++Idx) {
                    FakeSocket->PushBinary(WireFrame.GetData(), static_cast<SIZE_T>(WireFrame.Num()), true);
                }
                Transport.Tick(0.016f);
            }
        }
    );

    TestTrue(TEXT("WebSocket 零拷贝快路径收包数量大于 0"), ReceivedCount > 0);
    AddInfo(FString::Printf(
        TEXT("【基准】WebSocketRxZeroCopy: Median=%.2f ns/pkt, Mean=%.2f ns, CV=%.1f%%, 吞吐=%.2f Kops/s"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e3
    ));

    constexpr double BudgetNsPerOp = 1500.0;
    TestTrue(
        FString::Printf(TEXT("WebSocket 收包单帧中位耗时应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, Metrics.MedianNs),
        Metrics.MedianNs < BudgetNsPerOp
    );
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNetMessageDispatcherPerfTest,
    "Game.Net.Service.Perf.MessageDispatcher",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 多轮基准测试消息分发器多监听槽位零分配派发性能
 * @param Parameters 自动化测试参数
 * @return 测试是否通过性能预算
 */
bool FNetMessageDispatcherPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetMessageDispatcher Dispatcher;

    int32 HandlerACount = 0;
    int32 HandlerBCount = 0;
    const int64 HandleA = Dispatcher.RegisterHandler(5001, [&HandlerACount](const FNetPacketHeader&, TConstArrayView<uint8>) {
        ++HandlerACount;
    });
    const int64 HandleB = Dispatcher.RegisterHandler(5001, [&HandlerBCount](const FNetPacketHeader&, TConstArrayView<uint8>) {
        ++HandlerBCount;
    });
    TestTrue(TEXT("双监听器注册有效"), HandleA > 0 && HandleB > 0);

    FNetPacketHeader Header;
    Header.MessageId = 5001;
    const uint8 PayloadData[] = {1, 2, 3, 4, 5, 6, 7, 8};

    constexpr int32 IterationsPerRound = 20000;
    const FNetServiceBenchmarkMetrics Metrics = RunServiceBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&Dispatcher, &Header, &PayloadData](int32 Count) {
            for (int32 Index = 0; Index < Count; ++Index) {
                (void)Dispatcher.DispatchPacket(Header, PayloadData);
            }
        }
    );

    TestTrue(TEXT("HandlerA 与 HandlerB 命中次数一致"), HandlerACount == HandlerBCount && HandlerACount > 0);
    AddInfo(FString::Printf(
        TEXT("【基准】MessageDispatcher(2 slots): Median=%.2f ns, Mean=%.2f ns, CV=%.1f%%, 吞吐=%.2f Mops/s"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));

    constexpr double BudgetNsPerOp = 300.0;
    TestTrue(
        FString::Printf(TEXT("多槽位派发中位耗时应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, Metrics.MedianNs),
        Metrics.MedianNs < BudgetNsPerOp
    );
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNetMessageBusBridgeShortCircuitPerfTest,
    "Game.Net.Service.Perf.MessageBusBridgeNoListener",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 多轮基准测试消息总线桥接层在无订阅者时的零分配短路开销
 * @param Parameters 自动化测试参数
 * @return 测试是否通过性能预算
 */
bool FNetMessageBusBridgeShortCircuitPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameNetBridgePerfWorld")));
    UGameplayMessageSubsystem& MessageBus = UGameplayMessageSubsystem::Get(GameInstance->GetWorld());

    FNetMessageBusBridge Bridge;
    Bridge.Bind(MessageBus, [](const FGameNetOutboundMessage&) {});

    FNetPacketHeader Header;
    Header.MessageId = 5002;
    const uint8 Payload[256] = {1, 2, 3, 4};

    constexpr int32 IterationsPerRound = 50000;
    const FNetServiceBenchmarkMetrics Metrics = RunServiceBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&Bridge, &Header, &Payload](int32 Count) {
            for (int32 Index = 0; Index < Count; ++Index) {
                Bridge.BroadcastInboundPacket(Header, Payload);
            }
        }
    );

    AddInfo(FString::Printf(
        TEXT("【基准】MessageBusBridgeNoListener(256B): Median=%.2f ns, Mean=%.2f ns, CV=%.1f%%, 吞吐=%.2f Mops/s"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));

    constexpr double BudgetNsPerOp = 250.0;
    TestTrue(
        FString::Printf(TEXT("无监听零分配短路中位耗时应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, Metrics.MedianNs),
        Metrics.MedianNs < BudgetNsPerOp
    );

    Bridge.Unbind();
    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetServiceSubsystemRpcPerfTest,
    "Game.Net.Service.Perf.SubsystemEndToEndRpc",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 多轮基准测试子系统经 MockTransport 执行端到端 Raw RPC 请求-响应闭环性能
 * @param Parameters 自动化测试参数
 * @return 测试是否通过性能预算
 */
bool FGameNetServiceSubsystemRpcPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameNetServiceRpcPerfWorld")));
    UGameNetServiceSubsystem& NetService = UGameNetServiceSubsystem::Get(GameInstance);

    const TSharedRef<FMockNetTransport> MockTransport = MakeShared<FMockNetTransport>();
    NetService.SetTransport(MockTransport);
    MockTransport->RegisterMockHandler(
        5001,
        5002,
        [](const FNetPacketHeader&, TConstArrayView<uint8> ReqBytes) -> TOptional<TArray<uint8>> {
            return TArray<uint8>(ReqBytes.GetData(), ReqBytes.Num());
        }
    );
    TestTrue(TEXT("连接回环 RPC 基准端点"), NetService.Connect(TEXT("mock://rpc_perf")));

    const uint8 ReqPayload[32] = {7, 8, 9, 10};
    int32 CompletedRpcs = 0;

    constexpr int32 IterationsPerRound = 5000;
    const FNetServiceBenchmarkMetrics Metrics = RunServiceBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&NetService, &ReqPayload, &CompletedRpcs](int32 Count) {
            for (int32 Index = 0; Index < Count; ++Index) {
                (void)NetService.SendRawRequest(
                    5001,
                    5002,
                    ReqPayload,
                    5.0f,
                    [&CompletedRpcs](ENetRequestResult Res, const FNetPacketHeader&, TConstArrayView<uint8>) {
                        if (Res == ENetRequestResult::Success) {
                            ++CompletedRpcs;
                        }
                    }
                );
                NetService.Tick(0.016f);
            }
        }
    );

    TestTrue(TEXT("端到端 RPC 全部完成"), CompletedRpcs > 0);
    AddInfo(FString::Printf(
        TEXT("【基准】SubsystemRawRpcRoundTrip: Median=%.2f ns, Mean=%.2f ns, CV=%.1f%%, 吞吐=%.2f Kops/s"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e3
    ));

    constexpr double BudgetNsPerOp = 5000.0;
    TestTrue(
        FString::Printf(TEXT("子系统端到端 Raw RPC 中位耗时应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, Metrics.MedianNs),
        Metrics.MedianNs < BudgetNsPerOp
    );

    GameInstance->Shutdown();
    return true;
}

#endif
