/**
 * @file UGameNetServicePerformanceTest.cpp
 * @brief GameNetService 模块微基准性能测试套件（重连退避 / 心跳保活 / 异步请求结算 / WebSocket分片拼装 / 多播协议路由）
 */

#include "CoreMinimal.h"

#if WITH_AUTOMATION_TESTS

#include "Dispatch/NetDispatchTypes.h"
#include "Dispatch/NetMessageDispatcher.h"
#include "Dispatch/NetPendingRequestTracker.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Policy/NetHeartbeatKeeper.h"
#include "Policy/NetPolicyTypes.h"
#include "Policy/NetReconnectPolicy.h"
#include "Transport/WebSocketFragmentAssembler.h"
#include "Transport/WebSocketTransportTypes.h"

/**
 * @brief 指数退避重连调度与计时器推进微基准性能测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNetReconnectPolicyPerfTest,
    "Game.Net.Service.Perf.ReconnectPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行指数退避重连计算与时钟步进性能测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FNetReconnectPolicyPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetReconnectConfig Config;
    Config.bEnableAutoReconnect = true;
    Config.BaseDelaySeconds = 0.5f;
    Config.MaxDelaySeconds = 16.0f;
    Config.MaxRetryAttempts = 10;
    Config.JitterRatio = 0.2f;

    FNetReconnectPolicy Policy(Config, 42);

    constexpr int32 WarmupIterations = 2000;
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        Policy.ScheduleNextAttempt();
        Policy.AdvanceTimer(1.0f);
        if (Policy.IsExhausted()) {
            Policy.Reset();
        }
    }
    Policy.Reset();

    constexpr int32 Iterations = 50000;
    int32 ScheduledCount = 0;

    const uint64 StartCycles = FPlatformTime::Cycles64();
    for (int32 Index = 0; Index < Iterations; ++Index) {
        const TOptional<float> Delay = Policy.ScheduleNextAttempt();
        if (Delay.IsSet()) {
            ++ScheduledCount;
            Policy.AdvanceTimer(*Delay);
        } else {
            Policy.Reset();
        }
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedSeconds = FPlatformTime::ToSeconds64(ElapsedCycles);
    const double ElapsedNs = ElapsedSeconds * 1e9;
    const double PerOpNs = ElapsedNs / static_cast<double>(Iterations);
    const double Mops = (static_cast<double>(Iterations) / ElapsedSeconds) / 1e6;

    TestTrue(TEXT("成功调度次数大于 0"), ScheduledCount > 0);

    AddInfo(FString::Printf(
        TEXT("【性能指标】ReconnectPolicy: 迭代=%d 次, 总耗时=%.2f ms, 单次均耗=%.2f ns, 吞吐=%.2f Mops/s"),
        Iterations,
        ElapsedSeconds * 1000.0,
        PerOpNs,
        Mops
    ));

    constexpr double BudgetNsPerOp = 200.0;
    TestTrue(
        FString::Printf(TEXT("重连退避计算单次均耗应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, PerOpNs),
        PerOpNs < BudgetNsPerOp
    );

    return true;
}

/**
 * @brief 周期心跳报文侦测、RTT 测算与下行到达通知微基准性能测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNetHeartbeatKeeperPerfTest,
    "Game.Net.Service.Perf.HeartbeatKeeper",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行心跳报文时延测算性能测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FNetHeartbeatKeeperPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetHeartbeatConfig Config;
    Config.bEnabled = true;
    Config.IntervalSeconds = 5.0f;
    Config.TimeoutSeconds = 15.0f;
    Config.PingMessageId = 101;
    Config.PongMessageId = 102;

    FNetHeartbeatKeeper Keeper(Config);

    constexpr int32 WarmupIterations = 2000;
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        Keeper.Tick(0.016f);
        Keeper.RecordPingSent(Index, 100.0);
        Keeper.NotifyInboundPacket(102, Index, 100.025);
    }
    Keeper.Reset();

    constexpr int32 Iterations = 100000;
    double SimulatedClock = 500.0;

    const uint64 StartCycles = FPlatformTime::Cycles64();
    for (int32 Index = 0; Index < Iterations; ++Index) {
        SimulatedClock += 0.016;
        Keeper.Tick(0.016f);
        Keeper.RecordPingSent(Index, SimulatedClock);
        Keeper.NotifyInboundPacket(102, Index, SimulatedClock + 0.022);
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedSeconds = FPlatformTime::ToSeconds64(ElapsedCycles);
    const double ElapsedNs = ElapsedSeconds * 1e9;
    const double PerOpNs = ElapsedNs / static_cast<double>(Iterations);
    const double Mops = (static_cast<double>(Iterations) / ElapsedSeconds) / 1e6;

    TestTrue(TEXT("最终成功测算 RTT"), Keeper.GetLastRttMilliseconds() > 0.0f);

    AddInfo(FString::Printf(
        TEXT("【性能指标】HeartbeatKeeper: 迭代=%d 次, 总耗时=%.2f ms, 单次均耗=%.2f ns, 吞吐=%.2f Mops/s"),
        Iterations,
        ElapsedSeconds * 1000.0,
        PerOpNs,
        Mops
    ));

    constexpr double BudgetNsPerOp = 100.0;
    TestTrue(
        FString::Printf(TEXT("心跳与RTT更新单次均耗应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, PerOpNs),
        PerOpNs < BudgetNsPerOp
    );

    return true;
}

/**
 * @brief 异步 Request-Response 挂起请求发号、登记与快速匹配结算微基准性能测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNetPendingRequestTrackerPerfTest,
    "Game.Net.Service.Perf.RequestTracker",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行异步请求生命周期结算性能测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FNetPendingRequestTrackerPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetPendingRequestTracker Tracker;

    constexpr int32 WarmupIterations = 2000;
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        const uint32 Seq = Tracker.AllocateSequenceId();
        Tracker.RegisterPendingRequest(Seq, 200, 5.0f, [](ENetRequestResult, const FNetPacketHeader&, TConstArrayView<uint8>) {});
        FNetPacketHeader RspHeader;
        RspHeader.MessageId = 200;
        RspHeader.SequenceId = Seq;
        Tracker.TryCompleteResponse(RspHeader, {});
    }

    constexpr int32 Iterations = 50000;
    int32 SettledCount = 0;
    const uint8 DummyPayload[] = {1, 2, 3, 4};

    const uint64 StartCycles = FPlatformTime::Cycles64();
    for (int32 Index = 0; Index < Iterations; ++Index) {
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
        Tracker.TryCompleteResponse(RspHeader, DummyPayload);
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedSeconds = FPlatformTime::ToSeconds64(ElapsedCycles);
    const double ElapsedNs = ElapsedSeconds * 1e9;
    const double PerOpNs = ElapsedNs / static_cast<double>(Iterations);
    const double Kops = (static_cast<double>(Iterations) / ElapsedSeconds) / 1e3;

    TestEqual(TEXT("全部请求成功结算"), SettledCount, Iterations);
    TestEqual(TEXT("结算后挂起数为 0"), Tracker.GetPendingCount(), 0);

    AddInfo(FString::Printf(
        TEXT("【性能指标】PendingRequestTracker: 迭代=%d 次, 总耗时=%.2f ms, 单次均耗=%.2f ns (%.2f μs), 吞吐=%.2f Kops/s"),
        Iterations,
        ElapsedSeconds * 1000.0,
        PerOpNs,
        PerOpNs / 1000.0,
        Kops
    ));

    constexpr double BudgetNsPerOp = 800.0;
    TestTrue(
        FString::Printf(TEXT("异步请求配对单次均耗应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, PerOpNs),
        PerOpNs < BudgetNsPerOp
    );

    return true;
}

/**
 * @brief WebSocket 二进制分片消息累加与就地拼装微基准性能测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWebSocketFragmentAssemblerPerfTest,
    "Game.Net.Service.Perf.FragmentAssembler",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行 WebSocket 分片流拼装性能测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FWebSocketFragmentAssemblerPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FWebSocketFragmentAssembler Assembler(65536);

    uint8 ChunkA[256];
    uint8 ChunkB[256];
    FMemory::Memset(ChunkA, 0xAA, sizeof(ChunkA));
    FMemory::Memset(ChunkB, 0xBB, sizeof(ChunkB));

    constexpr int32 WarmupIterations = 2000;
    TArray<uint8> WarmupMessage;
    WarmupMessage.Reserve(1024);
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        Assembler.AppendFragment(ChunkA, false, WarmupMessage);
        Assembler.AppendFragment(ChunkB, true, WarmupMessage);
    }
    Assembler.Reset();

    constexpr int32 Iterations = 20000;
    TArray<uint8> AssembledMessage;
    AssembledMessage.Reserve(1024);
    int32 CompleteMessageCount = 0;

    const uint64 StartCycles = FPlatformTime::Cycles64();
    for (int32 Index = 0; Index < Iterations; ++Index) {
        const EWebSocketFragmentStatus StatusA = Assembler.AppendFragment(ChunkA, false, AssembledMessage);
        const EWebSocketFragmentStatus StatusB = Assembler.AppendFragment(ChunkB, true, AssembledMessage);
        if (StatusA == EWebSocketFragmentStatus::Incomplete && StatusB == EWebSocketFragmentStatus::MessageReady) {
            ++CompleteMessageCount;
        }
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedSeconds = FPlatformTime::ToSeconds64(ElapsedCycles);
    const double ElapsedNs = ElapsedSeconds * 1e9;
    const double PerOpNs = ElapsedNs / static_cast<double>(Iterations);
    const double Kops = (static_cast<double>(Iterations) / ElapsedSeconds) / 1e3;
    const double TotalMB = (static_cast<double>(sizeof(ChunkA) + sizeof(ChunkB)) * Iterations) / (1024.0 * 1024.0);
    const double ThroughputMBs = TotalMB / ElapsedSeconds;

    TestEqual(TEXT("拼装成包总数与迭代一致"), CompleteMessageCount, Iterations);
    TestEqual(TEXT("单条拼装结果长度正确"), AssembledMessage.Num(), static_cast<int32>(sizeof(ChunkA) + sizeof(ChunkB)));

    AddInfo(FString::Printf(
        TEXT("【性能指标】FragmentAssembler: 迭代=%d 次(共 %d 分片), 总耗时=%.2f ms, 单次均耗=%.2f ns, 吞吐=%.2f Kops/s (%.2f MB/s)"),
        Iterations,
        Iterations * 2,
        ElapsedSeconds * 1000.0,
        PerOpNs,
        Kops,
        ThroughputMBs
    ));

    constexpr double BudgetNsPerOp = 2000.0;
    TestTrue(
        FString::Printf(TEXT("两分片拼装单次均耗应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, PerOpNs),
        PerOpNs < BudgetNsPerOp
    );

    return true;
}

/**
 * @brief 网络协议多播路由器快照安全与多槽位派发微基准性能测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNetMessageDispatcherPerfTest,
    "Game.Net.Service.Perf.MessageDispatcher",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行多播协议路由派发性能测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FNetMessageDispatcherPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetMessageDispatcher Dispatcher;

    int32 HandlerACount = 0;
    int32 HandlerBCount = 0;
    Dispatcher.RegisterHandler(5001, [&HandlerACount](const FNetPacketHeader&, TConstArrayView<uint8>) {
        ++HandlerACount;
    });
    Dispatcher.RegisterHandler(5001, [&HandlerBCount](const FNetPacketHeader&, TConstArrayView<uint8>) {
        ++HandlerBCount;
    });

    FNetPacketHeader Header;
    Header.MessageId = 5001;
    const uint8 PayloadData[] = {1, 2, 3, 4, 5, 6, 7, 8};

    constexpr int32 WarmupIterations = 2000;
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        Dispatcher.DispatchPacket(Header, PayloadData);
    }
    HandlerACount = 0;
    HandlerBCount = 0;

    constexpr int32 Iterations = 50000;

    const uint64 StartCycles = FPlatformTime::Cycles64();
    for (int32 Index = 0; Index < Iterations; ++Index) {
        Dispatcher.DispatchPacket(Header, PayloadData);
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedSeconds = FPlatformTime::ToSeconds64(ElapsedCycles);
    const double ElapsedNs = ElapsedSeconds * 1e9;
    const double PerOpNs = ElapsedNs / static_cast<double>(Iterations);
    const double Mops = (static_cast<double>(Iterations) / ElapsedSeconds) / 1e6;

    TestEqual(TEXT("HandlerA 命中次数符合预期"), HandlerACount, Iterations);
    TestEqual(TEXT("HandlerB 命中次数符合预期"), HandlerBCount, Iterations);

    AddInfo(FString::Printf(
        TEXT("【性能指标】MessageDispatcher: 迭代=%d 次(双槽位), 总耗时=%.2f ms, 单次均耗=%.2f ns, 吞吐=%.2f Mops/s"),
        Iterations,
        ElapsedSeconds * 1000.0,
        PerOpNs,
        Mops
    ));

    constexpr double BudgetNsPerOp = 300.0;
    TestTrue(
        FString::Printf(TEXT("多槽位派发单次均耗应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, PerOpNs),
        PerOpNs < BudgetNsPerOp
    );

    return true;
}

#endif
