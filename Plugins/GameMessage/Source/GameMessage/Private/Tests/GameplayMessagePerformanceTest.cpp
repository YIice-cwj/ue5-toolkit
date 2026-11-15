// Copyright Epic Games, Inc. All Rights Reserved.

#include "Message/GameplayMessageSubsystem.h"

#include "Tests/GameMessageTestReceiver.h"

#if WITH_AUTOMATION_TESTS

#include <atomic>

#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Async/TaskGraphInterfaces.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "HAL/PlatformTime.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/AutomationTest.h"

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 5;

constexpr int32 ExactMatchIterations = 100000;
constexpr double ExactMatchBudgetNsPerOp = 300.0;

constexpr int32 FanOutIterations = 20000;
constexpr double FanOut16BudgetNsPerOp = 1500.0;

constexpr int32 PartialMatchIterations = 50000;
constexpr double PartialMatchBudgetNsPerOp = 600.0;

constexpr int32 CrossThreadIterations = 20000;
constexpr double CrossThreadBudgetNsPerOp = 1000.0;

constexpr int32 ChurnIterations = 50000;
constexpr double ChurnBudgetNsPerOp = 600.0;

struct FBenchmarkMetrics {
    double MedianNs = 0.0;
    double MeanNs = 0.0;
    double CvPercent = 0.0;
    double MinNs = 0.0;
    double MaxNs = 0.0;
    double OpsPerSec = 0.0;
};

/**
 * @brief 执行多轮基准测试，消除预热偏差并统计中位数与离散度 (CV%)
 * @tparam TFunc 待测闭包类型
 * @param WarmupRounds 预热轮数（测量后丢弃）
 * @param SampleRounds 正式采样轮数（规范要求 >= 5 轮）
 * @param IterationsPerRound 每轮迭代执行次数
 * @param Body 待执行测量闭包
 * @return 包含中位数与离散度的统计度量
 */
template <typename TFunc>
FBenchmarkMetrics RunBenchmark(
    const int32 WarmupRounds,
    const int32 SampleRounds,
    const int32 IterationsPerRound,
    TFunc&& Body
) {
    for (int32 Round = 0; Round < WarmupRounds; ++Round) {
        Body(IterationsPerRound);
    }

    TArray<double> SamplesNs;
    SamplesNs.Reserve(SampleRounds);

    double SumNs = 0.0;
    for (int32 Round = 0; Round < SampleRounds; ++Round) {
        const double StartTime = FPlatformTime::Seconds();
        Body(IterationsPerRound);
        const double ElapsedSec = FPlatformTime::Seconds() - StartTime;
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
    const double CvPercent = (MeanNs > 0.0) ? ((StdDev / MeanNs) * 100.0) : 0.0;

    FBenchmarkMetrics Metrics;
    Metrics.MedianNs = MedianNs;
    Metrics.MeanNs = MeanNs;
    Metrics.CvPercent = CvPercent;
    Metrics.MinNs = SamplesNs[0];
    Metrics.MaxNs = SamplesNs.Last();
    Metrics.OpsPerSec = (MedianNs > 0.0) ? (1.0e9 / MedianNs) : 0.0;
    return Metrics;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMessageExactMatchBroadcastPerfTest,
    "Game.Message.Perf.ExactMatchBroadcast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 精确匹配单监听器主线程同步广播微基准（纳秒级延迟与吞吐）
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且吞吐符合预期返回 true
 */
bool FGameMessageExactMatchBroadcastPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>();
    UGameplayMessageSubsystem* Subsystem = NewObject<UGameplayMessageSubsystem>(GameInstance);

    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.Message.Exact")));

    volatile int64 Sink = 0;
    FGameplayMessageListenerHandle Handle = Subsystem->RegisterListener<FTestGameplayMessage>(
        Channel,
        [&Sink](FGameplayTag, const FTestGameplayMessage& Payload) {
            Sink = Sink + Payload.Value;
        }
    );

    FTestGameplayMessage Message;
    Message.Value = 1;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ExactMatchIterations,
        [Subsystem, Channel, &Message](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Subsystem->BroadcastMessage(Channel, Message);
            }
        }
    );

    Handle.Unregister();

    TestTrue(
        FString::Printf(TEXT("精确广播耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, ExactMatchBudgetNsPerOp),
        Metrics.MedianNs < ExactMatchBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("ExactMatch Broadcast: %.1f ns/op (CV: %.2f%%, %.2f Mops/s, Min: %.1f ns, Max: %.1f ns, Sink: %lld)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6,
        Metrics.MinNs,
        Metrics.MaxNs,
        Sink
    ));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMessageFanOutScalabilityPerfTest,
    "Game.Message.Perf.FanOutScalability",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 多监听器广播扇出扩展性微基准（对比 1 / 4 / 8 / 16 / 32 监听器栈内联与堆分配性能）
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且扇出扩展符合预期返回 true
 */
bool FGameMessageFanOutScalabilityPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>();
    UGameplayMessageSubsystem* Subsystem = NewObject<UGameplayMessageSubsystem>(GameInstance);

    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.Message.FanOut")));

    volatile int64 Sink = 0;
    FTestGameplayMessage Message;
    Message.Value = 1;

    auto BenchmarkWithListeners = [&](int32 ListenerCount) -> FBenchmarkMetrics {
        TArray<FGameplayMessageListenerHandle> Handles;
        Handles.Reserve(ListenerCount);
        for (int32 Idx = 0; Idx < ListenerCount; ++Idx) {
            Handles.Add(Subsystem->RegisterListener<FTestGameplayMessage>(
                Channel,
                [&Sink](FGameplayTag, const FTestGameplayMessage& Payload) {
                    Sink = Sink + Payload.Value;
                }
            ));
        }

        const FBenchmarkMetrics Metrics = RunBenchmark(
            DefaultWarmupRounds,
            DefaultSampleRounds,
            FanOutIterations,
            [Subsystem, Channel, &Message](const int32 Iterations) {
                for (int32 Index = 0; Index < Iterations; ++Index) {
                    Subsystem->BroadcastMessage(Channel, Message);
                }
            }
        );

        for (FGameplayMessageListenerHandle& H : Handles) {
            H.Unregister();
        }

        return Metrics;
    };

    const FBenchmarkMetrics M1 = BenchmarkWithListeners(1);
    const FBenchmarkMetrics M4 = BenchmarkWithListeners(4);
    const FBenchmarkMetrics M8 = BenchmarkWithListeners(8);
    const FBenchmarkMetrics M16 = BenchmarkWithListeners(16);
    const FBenchmarkMetrics M32 = BenchmarkWithListeners(32);

    TestTrue(
        FString::Printf(TEXT("16 监听器栈内联广播耗时 %.1f ns/op 应低于预算 %.0f ns/op"), M16.MedianNs, FanOut16BudgetNsPerOp),
        M16.MedianNs < FanOut16BudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("Fan-Out Scaling: [1 Listener: %.1f ns (%.2f Mops/s)] | [4: %.1f ns] | [8: %.1f ns] | [16 (Inline Limit): %.1f ns] | [32 (Heap Fallback): %.1f ns]"),
        M1.MedianNs, M1.OpsPerSec / 1.0e6,
        M4.MedianNs,
        M8.MedianNs,
        M16.MedianNs,
        M32.MedianNs
    ));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMessagePartialMatchPerfTest,
    "Game.Message.Perf.PartialMatchVsExactMatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 层级父子标签前缀匹配 vs 精确匹配开销微基准（验证 PartialMatch 计数快速路径优化收益）
 *
 * 测试执行两阶段对比：
 * 阶段 1：仅存在精确匹配监听器时（触发 PartialMatchListenerCount == 0 快速路径）；
 * 阶段 2：存在父标签前缀匹配监听器时（走完整父标签树提取与逐级查找）。
 *
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且部分匹配开销符合预期返回 true
 */
bool FGameMessagePartialMatchPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>();
    UGameplayMessageSubsystem* Subsystem = NewObject<UGameplayMessageSubsystem>(GameInstance);

    const FGameplayTag ParentChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.Combat")));
    const FGameplayTag LeafChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.Combat.Skill.Damage")));

    volatile int64 Sink = 0;
    FTestGameplayMessage Message;
    Message.Value = 1;

    FGameplayMessageListenerHandle ExactHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        LeafChannel,
        [&Sink](FGameplayTag, const FTestGameplayMessage& Payload) {
            Sink = Sink + Payload.Value;
        },
        EGameplayMessageMatch::ExactMatch
    );

    const FBenchmarkMetrics FastPathMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        PartialMatchIterations,
        [Subsystem, LeafChannel, &Message](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Subsystem->BroadcastMessage(LeafChannel, Message);
            }
        }
    );
    ExactHandle.Unregister();

    FGameplayMessageListenerHandle PartialHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        ParentChannel,
        [&Sink](FGameplayTag, const FTestGameplayMessage& Payload) {
            Sink = Sink + Payload.Value;
        },
        EGameplayMessageMatch::PartialMatch
    );

    const FBenchmarkMetrics HierarchyMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        PartialMatchIterations,
        [Subsystem, LeafChannel, &Message](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Subsystem->BroadcastMessage(LeafChannel, Message);
            }
        }
    );
    PartialHandle.Unregister();

    TestTrue(
        FString::Printf(TEXT("层级匹配广播耗时 %.1f ns/op 应低于预算 %.0f ns/op"), HierarchyMetrics.MedianNs, PartialMatchBudgetNsPerOp),
        HierarchyMetrics.MedianNs < PartialMatchBudgetNsPerOp
    );

    const double OverheadNs = HierarchyMetrics.MedianNs - FastPathMetrics.MedianNs;
    const double FastPathSpeedup = (HierarchyMetrics.MedianNs > 0.0) ? (HierarchyMetrics.MedianNs / FastPathMetrics.MedianNs) : 0.0;

    AddInfo(FString::Printf(
        TEXT("Fast-Path (Exact): %.1f ns/op (%.2f Mops/s) | Hierarchy Traversal (Partial): %.1f ns/op | Tag Tree Overhead: +%.1f ns | Fast-Path Speedup: %.2fx"),
        FastPathMetrics.MedianNs,
        FastPathMetrics.OpsPerSec / 1.0e6,
        HierarchyMetrics.MedianNs,
        OverheadNs,
        FastPathSpeedup
    ));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMessageCrossThreadEnqueuePerfTest,
    "Game.Message.Perf.CrossThreadEnqueueAndDrain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 多线程并发 EnqueueMessage 与主线程串行排空微基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且多线程无锁入队吞吐符合预期返回 true
 */
bool FGameMessageCrossThreadEnqueuePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>();
    UGameplayMessageSubsystem* Subsystem = NewObject<UGameplayMessageSubsystem>(GameInstance);

    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.Message.CrossThread")));

    std::atomic<int64> ReceivedSum{0};
    FGameplayMessageListenerHandle Handle = Subsystem->RegisterListener<FTestGameplayMessage>(
        Channel,
        [&ReceivedSum](FGameplayTag, const FTestGameplayMessage& Payload) {
            ReceivedSum.fetch_add(Payload.Value, std::memory_order_relaxed);
        }
    );

    constexpr int32 WorkerCount = 8;
    const int32 MsgsPerWorker = CrossThreadIterations / WorkerCount;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        CrossThreadIterations,
        [Subsystem, Channel, WorkerCount, MsgsPerWorker]([[maybe_unused]] const int32 TotalIterations) {
            ParallelFor(WorkerCount, [Subsystem, Channel, MsgsPerWorker](int32 WorkerIdx) {
                for (int32 Step = 0; Step < MsgsPerWorker; ++Step) {
                    FTestGameplayMessage Msg;
                    Msg.Value = WorkerIdx + 1;
                    Subsystem->EnqueueMessage(Channel, MoveTemp(Msg));
                }
            });

            Subsystem->FlushPendingMessages();
        }
    );

    Handle.Unregister();

    TestTrue(
        FString::Printf(TEXT("跨线程并发入队+主线程排空耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, CrossThreadBudgetNsPerOp),
        Metrics.MedianNs < CrossThreadBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("MPSC Cross-Thread Enqueue + Drain (8 Threads, %d msgs/round): %.1f ns/op (CV: %.2f%%, %.2f Mops/s, Sum: %lld)"),
        CrossThreadIterations,
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6,
        ReceivedSum.load(std::memory_order_relaxed)
    ));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMessageRegisterUnregisterPerfTest,
    "Game.Message.Perf.RegisterUnregisterChurn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 监听器动态注册与 RAII 退订吞吐微基准（验证高频短生命周期订阅性能）
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且注册退订性能符合预期返回 true
 */
bool FGameMessageRegisterUnregisterPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>();
    UGameplayMessageSubsystem* Subsystem = NewObject<UGameplayMessageSubsystem>(GameInstance);

    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.Message.Churn")));

    volatile int64 Sink = 0;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ChurnIterations,
        [Subsystem, Channel, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                FGameplayMessageListenerHandle Handle = Subsystem->RegisterListener<FTestGameplayMessage>(
                    Channel,
                    [&Sink](FGameplayTag, const FTestGameplayMessage& Payload) {
                        Sink = Sink + Payload.Value;
                    }
                );
                Handle.Unregister();
            }
        }
    );

    TestEqual(TEXT("全部退订后监听器列表清空"), Subsystem->GetListenerCount(), 0);

    TestTrue(
        FString::Printf(TEXT("动态注册+退订耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, ChurnBudgetNsPerOp),
        Metrics.MedianNs < ChurnBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("Register + Unregister Churn: %.1f ns/op (CV: %.2f%%, %.2f Mops/s, Final Listeners: %d)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6,
        Subsystem->GetListenerCount()
    ));

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
