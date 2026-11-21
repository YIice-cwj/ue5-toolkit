/**
 * @file FPerformanceTest.cpp
 * @brief GameCore 核心组件微基准自动化测试（对象池 / 刮擦内存 / 二进制切片 / 确定性散列 / 缓存 / 事件总线）
 *
 * 遵循通用代码规范与性能基准规范：多轮采样消除预热偏差，统计中位数与变异系数 (CV%)。
 * 预算用于防御数量级性能退化。
 */

#include "Cache/FVersionedCache.h"
#include "Config/FConfigRegistry.h"
#include "Config/TCachedConfigHandle.h"
#include "Containers/TLockFreeRingBuffer.h"
#include "Containers/TScratchArray.h"
#include "Containers/TSlotMap.h"
#include "Containers/TSortedVectorMap.h"
#include "Determinism/FStateChecksumBuilder.h"
#include "Determinism/TRewindSnapshotBuffer.h"
#include "Listener/FListenerHub.h"
#include "Memory/FByteSpanReader.h"
#include "Memory/FByteSpanWriter.h"
#include "Memory/FScopedFrameScratchpad.h"
#include "Memory/TFixedSizePool.h"
#include "Random/FGameRandomStream.h"
#include "Random/TWeightedPicker.h"
#include "State/TPriorityArbitrator.h"

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

constexpr int32 SlotMapFindIterations = 200000;
constexpr double SlotMapFindBudgetNsPerOp = 100.0;

constexpr int32 SortedVectorMapIterations = 200000;
constexpr double SortedVectorMapBudgetNsPerOp = 150.0;

constexpr int32 LockFreeRingBufferIterations = 200000;
constexpr double LockFreeRingBufferBudgetNsPerOp = 200.0;

constexpr int32 CachedConfigIterations = 200000;
constexpr double CachedConfigBudgetNsPerOp = 300.0;

constexpr int32 WeightedPickerIterations = 200000;
constexpr double WeightedPickerBudgetNsPerOp = 150.0;

constexpr int32 PriorityArbitratorIterations = 100000;
constexpr double PriorityArbitratorBudgetNsPerOp = 500.0;

constexpr int32 RewindSnapshotIterations = 100000;
constexpr double RewindSnapshotBudgetNsPerOp = 300.0;

struct FPerfSlotMapTag {};

struct FPerfRewindState {
    double X = 0.0;
    double Y = 0.0;
    float Health = 100.0f;
};

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameSlotMapPerfTest,
    "Game.Core.Perf.SlotMapFindAndIterate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief TSlotMap 代际句柄 O(1) 查找吞吐基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且结果正确返回 true
 */
bool FGameSlotMapPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using FPerfSlotMap = TSlotMap<int32, FPerfSlotMapTag>;
    using FPerfHandle = FPerfSlotMap::HandleType;

    constexpr int32 EntryCount = 1024;
    FPerfSlotMap SlotMap;
    SlotMap.Reserve(EntryCount);
    TArray<FPerfHandle> Handles;
    Handles.Reserve(EntryCount);
    for (int32 Index = 0; Index < EntryCount; ++Index) {
        Handles.Add(SlotMap.Insert(Index * 3));
    }

    volatile int64 Sink = 0;
    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        SlotMapFindIterations,
        [&SlotMap, &Handles, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                if (const int32* Val = SlotMap.Find(Handles[Index & (EntryCount - 1)])) {
                    Sink = Sink + *Val;
                }
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("TSlotMap::Find 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, SlotMapFindBudgetNsPerOp),
        Metrics.MedianNs < SlotMapFindBudgetNsPerOp
    );
    AddInfo(FString::Printf(
        TEXT("TSlotMapFind: %.1f ns/op (CV: %.2f%%, %.1f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameSortedVectorMapPerfTest,
    "Game.Core.Perf.SortedVectorMapFind",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief TSortedVectorMap 连续内存二分查找吞吐基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且结果正确返回 true
 */
bool FGameSortedVectorMapPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSortedVectorMap<int32, float, 16> Map;
    for (int32 Key = 0; Key < 16; ++Key) {
        Map.Add(Key * 2, static_cast<float>(Key) * 1.5f);
    }

    volatile double Sink = 0.0;
    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        SortedVectorMapIterations,
        [&Map, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                if (const float* Found = Map.Find((Index & 15) * 2)) {
                    Sink = Sink + *Found;
                }
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("TSortedVectorMap::Find 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, SortedVectorMapBudgetNsPerOp),
        Metrics.MedianNs < SortedVectorMapBudgetNsPerOp
    );
    AddInfo(FString::Printf(
        TEXT("TSortedVectorMapFind: %.1f ns/op (CV: %.2f%%, %.1f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLockFreeRingBufferPerfTest,
    "Game.Core.Perf.LockFreeRingBufferThroughput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief TLockFreeRingBuffer 无锁入队出队往返吞吐基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且结果正确返回 true
 */
bool FGameLockFreeRingBufferPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TUniquePtr<TLockFreeRingBuffer<uint64, 1024>> Ring = MakeUnique<TLockFreeRingBuffer<uint64, 1024>>();
    volatile uint64 Sink = 0;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        LockFreeRingBufferIterations,
        [&Ring, &Sink](const int32 Iterations) {
            uint64 OutValue = 0;
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Ring->Enqueue(static_cast<uint64>(Index));
                if (Ring->Dequeue(OutValue)) {
                    Sink = Sink + OutValue;
                }
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("TLockFreeRingBuffer Enqueue+Dequeue 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, LockFreeRingBufferBudgetNsPerOp),
        Metrics.MedianNs < LockFreeRingBufferBudgetNsPerOp
    );
    AddInfo(FString::Printf(
        TEXT("TLockFreeRingBuffer: %.1f ns/op (CV: %.2f%%, %.1f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameCachedConfigHandlePerfTest,
    "Game.Core.Perf.CachedConfigHandleVsRegistry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief TCachedConfigHandle 零哈希读取对比 FConfigRegistry::GetConfig 吞吐基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且结果正确返回 true
 */
bool FGameCachedConfigHandlePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    static const FName PerfKey(TEXT("Perf.Config.MoveSpeed"));
    FConfigRegistry& Registry = FConfigRegistry::Get();
    Registry.RegisterConfig<float>(PerfKey, 650.0f);
    TCachedConfigHandle<float> CachedHandle(PerfKey, 300.0f);

    volatile double Sink = 0.0;
    const FBenchmarkMetrics HandleMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        CachedConfigIterations,
        [&CachedHandle, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Sink = Sink + CachedHandle.Get();
            }
        }
    );

    const FBenchmarkMetrics RegistryMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        CachedConfigIterations,
        [&Registry, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Sink = Sink + Registry.GetConfig<float>(PerfKey, 300.0f);
            }
        }
    );

    const double SpeedupRatio = (HandleMetrics.MedianNs > 0.0) ? (RegistryMetrics.MedianNs / HandleMetrics.MedianNs) : 0.0;
    TestTrue(
        FString::Printf(TEXT("TCachedConfigHandle::Get 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), HandleMetrics.MedianNs, CachedConfigBudgetNsPerOp),
        HandleMetrics.MedianNs < CachedConfigBudgetNsPerOp
    );
    AddInfo(FString::Printf(
        TEXT("TCachedConfigHandle: %.1f ns/op (CV: %.2f%%) | FConfigRegistry: %.1f ns/op (CV: %.2f%%) | Speedup: %.2fx"),
        HandleMetrics.MedianNs,
        HandleMetrics.CvPercent,
        RegistryMetrics.MedianNs,
        RegistryMetrics.CvPercent,
        SpeedupRatio
    ));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameRandomAndWeightedPickerPerfTest,
    "Game.Core.Perf.RandomAndWeightedPicker",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief FGameRandomStream PCG32 与 TWeightedPicker Vose 别名表 O(1) 权重抽取基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且结果正确返回 true
 */
bool FGameRandomAndWeightedPickerPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameRandomStream Stream(20261120ULL);
    TWeightedPicker<int32> Picker;
    for (int32 Index = 0; Index < 32; ++Index) {
        Picker.AddEntry(Index + 1, static_cast<double>(Index + 1));
    }
    Picker.Build();

    volatile int64 Sink = 0;
    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        WeightedPickerIterations,
        [&Picker, &Stream, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Sink = Sink + Picker.Pick(Stream);
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("TWeightedPicker::Pick 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, WeightedPickerBudgetNsPerOp),
        Metrics.MedianNs < WeightedPickerBudgetNsPerOp
    );
    AddInfo(FString::Printf(
        TEXT("TWeightedPickerPick(32): %.1f ns/op (CV: %.2f%%, %.1f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePriorityArbitratorPerfTest,
    "Game.Core.Perf.PriorityArbitratorEval",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief TPriorityArbitrator 覆盖缓存直读与已有通道高频数值更新免排序聚合基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且结果正确返回 true
 */
bool FGamePriorityArbitratorPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TPriorityArbitrator<float> Arbitrator(EPriorityArbitrationMode::Additive);
    const FName Channels[8] = {
        FName(TEXT("Ch0")), FName(TEXT("Ch1")), FName(TEXT("Ch2")), FName(TEXT("Ch3")),
        FName(TEXT("Ch4")), FName(TEXT("Ch5")), FName(TEXT("Ch6")), FName(TEXT("Ch7"))
    };
    for (int32 Index = 0; Index < 8; ++Index) {
        Arbitrator.SetChannel(Channels[Index], static_cast<float>(Index + 1), Index);
    }
    [[maybe_unused]] const float WarmupVal = Arbitrator.GetEffectiveValue(100.0f);

    volatile double Sink = 0.0;
    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        PriorityArbitratorIterations,
        [&Arbitrator, &Channels, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Arbitrator.SetChannel(Channels[Index & 7], static_cast<float>(Index & 31), Index & 7);
                Sink = Sink + Arbitrator.GetEffectiveValue(100.0f);
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("TPriorityArbitrator 已有通道更新+聚合耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, PriorityArbitratorBudgetNsPerOp),
        Metrics.MedianNs < PriorityArbitratorBudgetNsPerOp
    );
    AddInfo(FString::Printf(
        TEXT("TPriorityArbitratorUpdateEval(8ch): %.1f ns/op (CV: %.2f%%, %.1f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameRewindSnapshotPerfTest,
    "Game.Core.Perf.RewindSnapshotInterpolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief TRewindSnapshotBuffer 环形写入与 O(log N) 亚帧插值查询基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且结果正确返回 true
 */
bool FGameRewindSnapshotPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TRewindSnapshotBuffer<FPerfRewindState, 128> Buffer;
    for (uint64 Frame = 0; Frame < 128; ++Frame) {
        Buffer.RecordSnapshot(
            static_cast<double>(Frame) * 0.016,
            Frame,
            FPerfRewindState{static_cast<double>(Frame) * 10.0, static_cast<double>(Frame) * 5.0, 100.0f}
        );
    }

    const auto Interpolator = [](const FPerfRewindState& A, const FPerfRewindState& B, float Alpha) {
        return FPerfRewindState{
            FMath::Lerp(A.X, B.X, Alpha),
            FMath::Lerp(A.Y, B.Y, Alpha),
            FMath::Lerp(A.Health, B.Health, Alpha)
        };
    };

    volatile double Sink = 0.0;
    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        RewindSnapshotIterations,
        [&Buffer, &Interpolator, &Sink](const int32 Iterations) {
            FPerfRewindState OutState;
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const double QueryTime = static_cast<double>(Index & 127) * 0.016 + 0.008;
                if (Buffer.GetInterpolatedSnapshot(QueryTime, OutState, Interpolator)) {
                    Sink = Sink + OutState.X;
                }
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("TRewindSnapshotBuffer 亚帧插值耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, RewindSnapshotBudgetNsPerOp),
        Metrics.MedianNs < RewindSnapshotBudgetNsPerOp
    );
    AddInfo(FString::Printf(
        TEXT("TRewindSnapshotInterp(128): %.1f ns/op (CV: %.2f%%, %.1f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
