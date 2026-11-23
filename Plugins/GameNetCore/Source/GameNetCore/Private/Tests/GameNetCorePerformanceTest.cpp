/**
 * @file GameNetCorePerformanceTest.cpp
 * @brief GameNetCore 核心网络链路多轮统计微基准性能测试套件（帧就地编码 / 头部反解 / 环形流跨界解帧 / Mock 回环 / 二进制与 JSON 往返）
 *
 * 遵循通用代码规范与 UE5 性能基准规范：多轮预热与采样（>= 5 轮），统计中位数、均值、Min/Max 与变异系数 (CV%)。
 */

#include "CoreMinimal.h"

#if WITH_AUTOMATION_TESTS

#include "Codec/NetPacketCodec.h"
#include "Codec/NetPacketRingBuffer.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Serializer/JsonNetSerializer.h"
#include "Serializer/MemoryArchiveNetSerializer.h"
#include "Tests/GameNetCoreTestTypes.h"
#include "Transport/MockNetTransport.h"

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 5;

struct FNetCoreBenchmarkMetrics {
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
 * @return 包含中位数、极值与离散度的统计度量
 */
template <typename TFunc>
FNetCoreBenchmarkMetrics RunNetCoreBenchmark(
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

    FNetCoreBenchmarkMetrics Metrics;
    Metrics.MedianNs = MedianNs;
    Metrics.MeanNs = MeanNs;
    Metrics.CvPercent = (MeanNs > 0.0) ? ((StdDev / MeanNs) * 100.0) : 0.0;
    Metrics.MinNs = SamplesNs[0];
    Metrics.MaxNs = SamplesNs.Last();
    Metrics.OpsPerSec = (MedianNs > 0.0) ? (1.0e9 / MedianNs) : 0.0;
    return Metrics;
}

/**
 * @brief 构造用于性能基准测试的典型复杂结构体样本
 * @return 包含标量、字符串、数组与嵌套结构体的实例
 */
FGameNetTestPurchaseRsp BuildPerfSamplePurchaseRsp() {
    FGameNetTestPurchaseRsp Sample;
    Sample.bSuccess = true;
    Sample.RemainingGold = 9999;
    Sample.DiscountRate = 0.85f;
    Sample.ReceiptMessage = TEXT("Order_20261123_PerformanceTestToken");
    Sample.GrantedSerialNumbers = {1001, 1002, 1003, 1004, 1005};
    Sample.BonusItem.ItemCode = FName(TEXT("SuperPotion"));
    Sample.BonusItem.Quantity = 10;
    return Sample;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetPacketCodecEncodePerfTest,
    "Game.Net.Core.Perf.PacketCodecEncode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行报文帧就地编码与预分配复用多轮微基准测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameNetPacketCodecEncodePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetPacketHeader Header;
    Header.MessageId = 1001;
    Header.SequenceId = 42;
    const uint8 PayloadBytes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const TConstArrayView<uint8> PayloadView(PayloadBytes, sizeof(PayloadBytes));

    TArray<uint8> ReusableBuffer;
    ReusableBuffer.Reserve(64);
    volatile uint8 ByteSink = 0;

    constexpr int32 IterationsPerRound = 50000;
    const FNetCoreBenchmarkMetrics Metrics = RunNetCoreBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&Header, PayloadView, &ReusableBuffer, &ByteSink](int32 Count) {
            for (int32 Index = 0; Index < Count; ++Index) {
                FNetPacketCodec::EncodeFrameTo(Header, PayloadView, ReusableBuffer);
                ByteSink = ReusableBuffer[0];
            }
        }
    );

    TestEqual(TEXT("编码单帧总长度符合 16+16=32 字节"), ReusableBuffer.Num(), 32);
    AddInfo(FString::Printf(
        TEXT("【基准】PacketCodecEncodeTo: Median=%.2f ns, Mean=%.2f ns, Min=%.2f, Max=%.2f, CV=%.1f%%, 吞吐=%.2f Mops/s"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.MinNs,
        Metrics.MaxNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));

    constexpr double BudgetNsPerOp = 200.0;
    TestTrue(
        FString::Printf(TEXT("帧就地编码中位耗时应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, Metrics.MedianNs),
        Metrics.MedianNs < BudgetNsPerOp
    );
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetPacketCodecDecodePerfTest,
    "Game.Net.Core.Perf.PacketCodecDecode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行 16 字节报文头零分配反解多轮微基准测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameNetPacketCodecDecodePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetPacketHeader OriginalHeader;
    OriginalHeader.MessageId = 8888;
    OriginalHeader.SequenceId = 12345;
    OriginalHeader.PayloadLength = 64;
    uint8 WireBytes[FNetPacketHeader::WIRE_HEADER_BYTES];
    FNetPacketCodec::EncodeHeaderTo(OriginalHeader, WireBytes);

    FNetPacketHeader DecodedHeader;
    volatile uint32 MessageIdSink = 0;

    constexpr int32 IterationsPerRound = 100000;
    const FNetCoreBenchmarkMetrics Metrics = RunNetCoreBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&WireBytes, &DecodedHeader, &MessageIdSink](int32 Count) {
            for (int32 Index = 0; Index < Count; ++Index) {
                if (FNetPacketCodec::TryDecodeHeader(WireBytes, DecodedHeader)) {
                    MessageIdSink = MessageIdSink + DecodedHeader.MessageId;
                }
            }
        }
    );

    TestEqual(TEXT("反解报文头 MessageId 校验无误"), DecodedHeader.MessageId, 8888u);
    AddInfo(FString::Printf(
        TEXT("【基准】PacketCodecDecode: Median=%.2f ns, Mean=%.2f ns, Min=%.2f, Max=%.2f, CV=%.1f%%, 吞吐=%.2f Mops/s"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.MinNs,
        Metrics.MaxNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));

    constexpr double BudgetNsPerOp = 50.0;
    TestTrue(
        FString::Printf(TEXT("头部反解中位耗时应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, Metrics.MedianNs),
        Metrics.MedianNs < BudgetNsPerOp
    );
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetRingBufferWrapAndStickyPerfTest,
    "Game.Net.Core.Perf.RingBufferWrapAndSticky",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行环形缓冲区跨界环回与多帧粘包批量消费多轮微基准测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameNetRingBufferWrapAndStickyPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetPacketHeader Header;
    Header.MessageId = 2001;
    Header.SequenceId = 1;
    const uint8 PayloadData[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120};
    TArray<uint8> SingleFrame;
    FNetPacketCodec::EncodeFrameTo(Header, PayloadData, SingleFrame);

    TArray<uint8> StickyBatch;
    StickyBatch.Reserve(SingleFrame.Num() * 4);
    for (int32 BatchIdx = 0; BatchIdx < 4; ++BatchIdx) {
        StickyBatch.Append(SingleFrame);
    }

    FNetPacketRingBuffer RingBuffer(128, 256);
    const TConstArrayView<uint8> HalfPrefix(SingleFrame.GetData(), 12);
    const TConstArrayView<uint8> HalfSuffix(SingleFrame.GetData() + 12, SingleFrame.Num() - 12);
    TArray<uint8> StraddlingBatch;
    StraddlingBatch.Reserve(HalfSuffix.Num() + StickyBatch.Num() + HalfPrefix.Num());
    StraddlingBatch.Append(HalfSuffix.GetData(), HalfSuffix.Num());
    StraddlingBatch.Append(StickyBatch);
    StraddlingBatch.Append(HalfPrefix.GetData(), HalfPrefix.Num());

    volatile uint32 ChecksumSink = 0;
    int32 LastBatchConsumed = 0;

    constexpr int32 IterationsPerRound = 10000;
    const FNetCoreBenchmarkMetrics Metrics = RunNetCoreBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&RingBuffer, HalfPrefix, &StraddlingBatch, &ChecksumSink, &LastBatchConsumed](int32 Count) {
            RingBuffer.Reset();
            (void)RingBuffer.Append(HalfPrefix);
            for (int32 Index = 0; Index < Count; ++Index) {
                if (RingBuffer.Append(StraddlingBatch)) {
                    (void)RingBuffer.ConsumeAllAvailable(
                        [&ChecksumSink](const FNetPacketHeader& Hdr, TConstArrayView<uint8> Pld) {
                            ChecksumSink = ChecksumSink + Hdr.MessageId + (Pld.IsEmpty() ? 0 : Pld[0]);
                        },
                        &LastBatchConsumed
                    );
                }
            }
        }
    );

    TestEqual(TEXT("单次跨界粘包批量消费 5 帧并保留 12 字节半包驱动持续环回"), LastBatchConsumed, 5);
    TestEqual(TEXT("消费完毕后保留 12 字节尾部半包"), RingBuffer.GetReadableBytes(), 12);
    AddInfo(FString::Printf(
        TEXT("【基准】RingBufferWrapSticky5Frames: Median=%.2f ns/batch, Mean=%.2f ns, CV=%.1f%%, 吞吐=%.2f Mframes/s"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        (Metrics.OpsPerSec * 5.0) / 1.0e6
    ));

    constexpr double BudgetNsPerBatch = 1500.0;
    TestTrue(
        FString::Printf(TEXT("4 帧粘包解帧中位耗时应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerBatch, Metrics.MedianNs),
        Metrics.MedianNs < BudgetNsPerBatch
    );
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetMockTransportPerfTest,
    "Game.Net.Core.Perf.MockTransportLoopback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行 FMockNetTransport 单机回环发包与主线程派发多轮微基准测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameNetMockTransportPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FMockNetTransport Transport;
    Transport.SetEchoUnregisteredMessages(true);
    TestTrue(TEXT("回环基准连接成功"), Transport.Connect(TEXT("mock://bench")).IsSuccess());

    int32 ReceivedCount = 0;
    Transport.OnPacketReceived().AddLambda([&ReceivedCount](const FNetPacketHeader&, TConstArrayView<uint8>) {
        ++ReceivedCount;
    });

    FNetPacketHeader Header;
    Header.MessageId = 3001;
    Header.SequenceId = 1;
    const uint8 Payload[] = {1, 2, 3, 4, 5, 6, 7, 8};

    constexpr int32 IterationsPerRound = 10000;
    const FNetCoreBenchmarkMetrics Metrics = RunNetCoreBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&Transport, &Header, &Payload](int32 Count) {
            for (int32 Index = 0; Index < Count; ++Index) {
                (void)Transport.Send(Header, Payload);
            }
            Transport.Tick(0.016f);
        }
    );

    TestTrue(TEXT("回环累计收包大于零"), ReceivedCount > 0);
    AddInfo(FString::Printf(
        TEXT("【基准】MockTransportLoopback: Median=%.2f ns/pkt, Mean=%.2f ns, CV=%.1f%%, 吞吐=%.2f Mops/s"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));

    constexpr double BudgetNsPerOp = 2000.0;
    TestTrue(
        FString::Printf(TEXT("Mock 回环单包中位耗时应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, Metrics.MedianNs),
        Metrics.MedianNs < BudgetNsPerOp
    );
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetMemoryArchiveRoundTripPerfTest,
    "Game.Net.Core.Perf.MemoryArchiveRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行反射结构体紧凑二进制往返编解码多轮微基准测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameNetMemoryArchiveRoundTripPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameNetTestPurchaseRsp Source = BuildPerfSamplePurchaseRsp();
    const FMemoryArchiveNetSerializer Serializer;

    TArray<uint8> SerializedBuffer;
    SerializedBuffer.Reserve(256);
    FGameNetTestPurchaseRsp DecodedTarget;

    constexpr int32 IterationsPerRound = 4000;
    const FNetCoreBenchmarkMetrics Metrics = RunNetCoreBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&Serializer, &Source, &SerializedBuffer, &DecodedTarget](int32 Count) {
            for (int32 Index = 0; Index < Count; ++Index) {
                const TGameStatusResult<void> EncRes = Serializer.SerializeStructTo(Source, SerializedBuffer);
                if (EncRes.IsSuccess()) {
                    (void)Serializer.DeserializeStructTo(SerializedBuffer, DecodedTarget);
                }
            }
        }
    );

    TestEqual(TEXT("反序列化对象字段精确还原"), DecodedTarget.RemainingGold, Source.RemainingGold);
    TestEqual(TEXT("嵌套结构字段精确还原"), DecodedTarget.BonusItem.Quantity, Source.BonusItem.Quantity);
    AddInfo(FString::Printf(
        TEXT("【基准】MemoryArchiveRoundTrip: Median=%.2f μs, Mean=%.2f μs, CV=%.1f%%, 吞吐=%.0f ops/s, 字节数=%d"),
        Metrics.MedianNs / 1000.0,
        Metrics.MeanNs / 1000.0,
        Metrics.CvPercent,
        Metrics.OpsPerSec,
        SerializedBuffer.Num()
    ));

    constexpr double BudgetUsPerOp = 30.0;
    TestTrue(
        FString::Printf(TEXT("二进制 Archive 中位往返耗时应低于 %.1f μs (实测: %.2f μs)"), BudgetUsPerOp, Metrics.MedianNs / 1000.0),
        (Metrics.MedianNs / 1000.0) < BudgetUsPerOp
    );
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetJsonRoundTripPerfTest,
    "Game.Net.Core.Perf.JsonRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行反射结构体紧凑 UTF-8 JSON 往返编解码多轮微基准测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameNetJsonRoundTripPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameNetTestPurchaseRsp Source = BuildPerfSamplePurchaseRsp();
    const FJsonNetSerializer Serializer;

    TArray<uint8> JsonBuffer;
    JsonBuffer.Reserve(512);
    FGameNetTestPurchaseRsp DecodedTarget;

    constexpr int32 IterationsPerRound = 1500;
    const FNetCoreBenchmarkMetrics Metrics = RunNetCoreBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&Serializer, &Source, &JsonBuffer, &DecodedTarget](int32 Count) {
            for (int32 Index = 0; Index < Count; ++Index) {
                const TGameStatusResult<void> EncRes = Serializer.SerializeStructTo(Source, JsonBuffer);
                if (EncRes.IsSuccess()) {
                    (void)Serializer.DeserializeStructTo(JsonBuffer, DecodedTarget);
                }
            }
        }
    );

    TestEqual(TEXT("JSON 反序列化对象字段精确还原"), DecodedTarget.RemainingGold, Source.RemainingGold);
    TestEqual(TEXT("JSON 嵌套结构字段精确还原"), DecodedTarget.BonusItem.Quantity, Source.BonusItem.Quantity);
    AddInfo(FString::Printf(
        TEXT("【基准】JsonRoundTrip: Median=%.2f μs, Mean=%.2f μs, CV=%.1f%%, 吞吐=%.0f ops/s, 字节数=%d"),
        Metrics.MedianNs / 1000.0,
        Metrics.MeanNs / 1000.0,
        Metrics.CvPercent,
        Metrics.OpsPerSec,
        JsonBuffer.Num()
    ));

    constexpr double BudgetUsPerOp = 200.0;
    TestTrue(
        FString::Printf(TEXT("JSON 中位往返耗时应低于 %.1f μs (实测: %.2f μs)"), BudgetUsPerOp, Metrics.MedianNs / 1000.0),
        (Metrics.MedianNs / 1000.0) < BudgetUsPerOp
    );
    return true;
}

#endif
