/**
 * @file FPerformanceTest.cpp
 * @brief GameCore 核心组件微基准自动化测试（对象池 / 刮擦内存 / 二进制切片 / 确定性散列 / 缓存 / 事件总线）
 *
 * 遵循通用代码规范与性能基准规范：多轮采样消除预热偏差，统计中位数与变异系数 (CV%)。
 * 预算用于防御数量级性能退化。
 */

#include "Cache/FVersionedCache.h"
#include "Containers/TScratchArray.h"
#include "Determinism/FStateChecksumBuilder.h"
#include "Listener/FListenerHub.h"
#include "Memory/FByteSpanReader.h"
#include "Memory/FByteSpanWriter.h"
#include "Memory/FScopedFrameScratchpad.h"
#include "Memory/TFixedSizePool.h"

#if WITH_AUTOMATION_TESTS

#include "HAL/UnrealMemory.h"
#include "Misc/AutomationTest.h"

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 5;

constexpr int32 PerfCacheKeyCount = 4096;
constexpr int32 PerfCacheIterations = 200000;
constexpr double PerfCacheBudgetNsPerOp = 2000.0;

constexpr int32 PerfListenerIterations = 100000;
constexpr double PerfListenerBudgetNsPerOp = 5000.0;

constexpr int32 FixedSizePoolIterations = 200000;
constexpr double FixedSizePoolBudgetNsPerOp = 200.0;

constexpr int32 ScratchpadIterations = 20000;
constexpr double ScratchpadBudgetNsPerOp = 2000.0;

constexpr int32 ByteSpanIterations = 100000;
constexpr double ByteSpanBudgetNsPerOp = 300.0;

constexpr int32 ChecksumIterations = 100000;
constexpr double ChecksumBudgetNsPerOp = 500.0;

struct FPerfCacheValue {
    int32 Payload = 0;
};

struct FBenchmarkMetrics {
    double MedianNs = 0.0;
    double MeanNs = 0.0;
    double CvPercent = 0.0;
    double MinNs = 0.0;
    double MaxNs = 0.0;
    double OpsPerSec = 0.0;
};

struct FPerfTestPayload {
    uint64 FieldA = 0;
    uint64 FieldB = 0;
    uint64 FieldC = 0;
    uint64 FieldD = 0;
};

/**
 * @brief 执行多轮基准测试，消除预热偏差并统计中位数与离散度 (CV%)
 * @tparam TFunc 测量闭包类型
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
    FGameCacheFindPerfTest,
    "Game.Core.Perf.VersionedCacheFind",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief Eager 缓存查找吞吐基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且结果正确返回 true
 */
bool FGameCacheFindPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FVersionedCache<FPerfCacheValue> Cache(FName(TEXT("PerfCache")));

    TArray<FName> Keys;
    Keys.Reserve(PerfCacheKeyCount);
    for (int32 Index = 0; Index < PerfCacheKeyCount; ++Index) {
        Keys.Add(FName(*FString::Printf(TEXT("Perf.Cache.Key.%d"), Index)));
    }

    Cache.Rebuild(Keys, [](FName Key) {
        FPerfCacheValue Value;
        Value.Payload = static_cast<int32>(Key.ToString().Len());
        return Value;
    });

    int32 HitCount = 0;
    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        PerfCacheIterations,
        [&Cache, &Keys, &HitCount](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const FName& Key = Keys[Index % PerfCacheKeyCount];
                const TSharedPtr<const FPerfCacheValue> Value = Cache.FindShared(Key);
                if (Value.IsValid()) {
                    ++HitCount;
                }
            }
        }
    );

    TestEqual(TEXT("全部查找均应命中"), HitCount, PerfCacheIterations * (DefaultWarmupRounds + DefaultSampleRounds));
    TestTrue(
        FString::Printf(TEXT("Eager 缓存查找 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, PerfCacheBudgetNsPerOp),
        Metrics.MedianNs < PerfCacheBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("PerfCacheFind: %.1f ns/op (CV: %.2f%%, %.1f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameListenerHubPerfTest,
    "Game.Core.Perf.ListenerHubThroughput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 事件总线单订阅广播吞吐基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且回调次数正确返回 true
 */
bool FGameListenerHubPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    static const FName PerfEventName(TEXT("Perf.ListenerHub.Event"));

    int32 ReceivedCount = 0;
    const TSharedRef<FObserverSubscription> Subscription = FListenerHub::Get().Subscribe<int32>(
        PerfEventName,
        [&ReceivedCount](const int32& Payload) {
            ReceivedCount += Payload;
        }
    );

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        PerfListenerIterations,
        [](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                FListenerHub::Get().Emit<int32>(PerfEventName, 1);
            }
        }
    );

    TestEqual(TEXT("回调次数应与广播次数一致"), ReceivedCount, PerfListenerIterations * (DefaultWarmupRounds + DefaultSampleRounds));
    TestTrue(
        FString::Printf(TEXT("事件总线广播 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, PerfListenerBudgetNsPerOp),
        Metrics.MedianNs < PerfListenerBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("ListenerHubThroughput: %.1f ns/op (CV: %.2f%%, %.1f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFixedSizePoolPerfTest,
    "Game.Core.Perf.FixedSizePoolVsMalloc",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 定长类型化对象池对比原生 FMemory::Malloc/Free 吞吐基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且对象池性能优于堆分配返回 true
 */
bool FGameFixedSizePoolPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    volatile uint64 Sink = 0;

    TFixedSizePool<FPerfTestPayload, 2048> Pool;

    const FBenchmarkMetrics PoolMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        FixedSizePoolIterations,
        [&Pool, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                FPerfTestPayload* Item = Pool.New();
                Item->FieldA = static_cast<uint64>(Index);
                Sink = Sink + Item->FieldA;
                Pool.Free(Item);
            }
        }
    );

    const FBenchmarkMetrics MallocMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        FixedSizePoolIterations,
        [&Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                void* Ptr = FMemory::Malloc(sizeof(FPerfTestPayload));
                FPerfTestPayload* Item = static_cast<FPerfTestPayload*>(Ptr);
                Item->FieldA = static_cast<uint64>(Index);
                Sink = Sink + Item->FieldA;
                FMemory::Free(Ptr);
            }
        }
    );

    const double SpeedupRatio = (PoolMetrics.MedianNs > 0.0) ? (MallocMetrics.MedianNs / PoolMetrics.MedianNs) : 0.0;

    TestTrue(
        FString::Printf(TEXT("TFixedSizePool 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), PoolMetrics.MedianNs, FixedSizePoolBudgetNsPerOp),
        PoolMetrics.MedianNs < FixedSizePoolBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("TFixedSizePool: %.1f ns/op (CV: %.2f%%, %.1f Mops/s) | Malloc/Free: %.1f ns/op (CV: %.2f%%) | Speedup: %.2fx"),
        PoolMetrics.MedianNs,
        PoolMetrics.CvPercent,
        PoolMetrics.OpsPerSec / 1.0e6,
        MallocMetrics.MedianNs,
        MallocMetrics.CvPercent,
        SpeedupRatio
    ));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameScratchpadPerfTest,
    "Game.Core.Perf.ScratchpadVsHeapArray",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 单帧刮擦数组对比原生堆 TArray 分配吞吐基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且刮擦内存分配优于堆分配返回 true
 */
bool FGameScratchpadPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    volatile int32 Sink = 0;

    const FBenchmarkMetrics ScratchMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ScratchpadIterations,
        [&Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                FScopedFrameScratchpad FrameGuard;
                TScratchArray<int32> ScratchArray(FrameGuard, 128);
                for (int32 ItemIdx = 0; ItemIdx < 128; ++ItemIdx) {
                    ScratchArray.Add(ItemIdx);
                }
                Sink = Sink + ScratchArray[64];
            }
        }
    );

    const FBenchmarkMetrics HeapMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ScratchpadIterations,
        [&Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                TArray<int32> HeapArray;
                HeapArray.Reserve(128);
                for (int32 ItemIdx = 0; ItemIdx < 128; ++ItemIdx) {
                    HeapArray.Add(ItemIdx);
                }
                Sink = Sink + HeapArray[64];
            }
        }
    );

    const double SpeedupRatio = (ScratchMetrics.MedianNs > 0.0) ? (HeapMetrics.MedianNs / ScratchMetrics.MedianNs) : 0.0;

    TestTrue(
        FString::Printf(TEXT("TScratchArray 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), ScratchMetrics.MedianNs, ScratchpadBudgetNsPerOp),
        ScratchMetrics.MedianNs < ScratchpadBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("TScratchArray: %.1f ns/op (CV: %.2f%%, %.1f Kops/s) | Heap TArray: %.1f ns/op (CV: %.2f%%) | Speedup: %.2fx"),
        ScratchMetrics.MedianNs,
        ScratchMetrics.CvPercent,
        ScratchMetrics.OpsPerSec / 1.0e3,
        HeapMetrics.MedianNs,
        HeapMetrics.CvPercent,
        SpeedupRatio
    ));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameByteSpanPerfTest,
    "Game.Core.Perf.ByteSpanWriterReader",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 字节切片写入与只读解析往返吞吐基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且编解码吞吐符合预期返回 true
 */
bool FGameByteSpanPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    volatile int64 Sink = 0;
    uint8* RawBuffer = static_cast<uint8*>(FMemory::Malloc(256));

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ByteSpanIterations,
        [RawBuffer, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                FByteSpanWriter Writer(MakeArrayView(RawBuffer, 256));
                Writer.Write<int32>(Index);
                Writer.Write<float>(1.234f);
                Writer.Write<int64>(9876543210LL);
                Writer.Write<double>(3.1415926535);
                Writer.Write<bool>(true);

                FByteSpanReader Reader(Writer.GetWrittenSpan());
                int32 ValInt32 = 0;
                float ValFloat = 0.0f;
                int64 ValInt64 = 0;
                double ValDouble = 0.0;
                bool ValBool = false;

                Reader.Read(ValInt32);
                Reader.Read(ValFloat);
                Reader.Read(ValInt64);
                Reader.Read(ValDouble);
                Reader.Read(ValBool);

                Sink = Sink + ValInt32 + static_cast<int64>(ValFloat) + ValInt64 + static_cast<int64>(ValDouble) + (ValBool ? 1 : 0);
            }
        }
    );

    FMemory::Free(RawBuffer);

    constexpr double BytesPerOp = sizeof(int32) + sizeof(float) + sizeof(int64) + sizeof(double) + sizeof(bool);
    const double ThroughputMBs = (Metrics.MedianNs > 0.0) ? ((BytesPerOp / Metrics.MedianNs) * 1000.0) : 0.0;

    TestTrue(
        FString::Printf(TEXT("ByteSpan 编解码往返耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, ByteSpanBudgetNsPerOp),
        Metrics.MedianNs < ByteSpanBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("ByteSpanWriterReader: %.1f ns/op (CV: %.2f%%, %.1f Mops/s, %.1f MB/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6,
        ThroughputMBs
    ));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStateChecksumPerfTest,
    "Game.Core.Perf.StateChecksumMix64",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 确定性状态散列计算器 Mix64 吞吐基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且哈希计算吞吐符合预期返回 true
 */
bool FGameStateChecksumPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    volatile uint64 Sink = 0;
    const FName SampleName(TEXT("PlayerStateActor"));

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ChecksumIterations,
        [&SampleName, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                FStateChecksumBuilder Builder;
                Builder.AddInt32(Index);
                Builder.AddFloatQuantized(123.456f);
                Builder.AddInt64(9876543210LL);
                Builder.AddBool(Index % 2 == 0);
                Builder.AddName(SampleName);
                Builder.AddVectorQuantized(FVector(10.0, 20.0, 30.0));
                const uint64 Hash = Builder.Finalize();
                Sink = Sink + Hash;
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("StateChecksumMix64 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, ChecksumBudgetNsPerOp),
        Metrics.MedianNs < ChecksumBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("StateChecksumMix64: %.1f ns/op (CV: %.2f%%, %.1f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
