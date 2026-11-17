/**
 * @file UGameNetCorePerformanceTest.cpp
 * @brief GameNetCore 核心网络链路微基准性能测试套件（报文编码 / 头部快速反解 / 环形缓冲区零分配出包 / 二进制与JSON序列化往返）
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

namespace {

/**
 * @brief 构造用于性能基准测试的典型复杂结构体样本
 * @return 包含标量、字符串、数组与嵌套结构体的实例
 */
FGameNetTestPurchaseRsp BuildPerfSamplePurchaseRsp() {
    FGameNetTestPurchaseRsp Sample;
    Sample.bSuccess = true;
    Sample.RemainingGold = 9999;
    Sample.DiscountRate = 0.85f;
    Sample.ReceiptMessage = TEXT("Order_20261117_PerformanceTestToken");
    Sample.GrantedSerialNumbers = {1001, 1002, 1003, 1004, 1005};
    Sample.BonusItem.ItemCode = FName(TEXT("SuperPotion"));
    Sample.BonusItem.Quantity = 10;
    return Sample;
}

}  // namespace

/**
 * @brief 报文头与完整帧就地打包与复用缓冲微基准性能测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetPacketCodecEncodePerfTest,
    "Game.Net.Perf.PacketCodecEncode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行报文帧编码与预分配复用性能测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameNetPacketCodecEncodePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetPacketHeader Header;
    Header.MessageId = 1001;
    Header.SequenceId = 42;
    const uint8 PayloadBytes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const TConstArrayView<uint8> PayloadView(PayloadBytes, sizeof(PayloadBytes));

    constexpr int32 WarmupIterations = 2000;
    TArray<uint8> WarmupBuffer;
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        FNetPacketCodec::EncodeFrameTo(Header, PayloadView, WarmupBuffer);
    }

    constexpr int32 Iterations = 100000;
    TArray<uint8> ReusableBuffer;
    ReusableBuffer.Reserve(64);

    const uint64 StartCycles = FPlatformTime::Cycles64();
    for (int32 Index = 0; Index < Iterations; ++Index) {
        FNetPacketCodec::EncodeFrameTo(Header, PayloadView, ReusableBuffer);
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedSeconds = FPlatformTime::ToSeconds64(ElapsedCycles);
    const double ElapsedNs = ElapsedSeconds * 1e9;
    const double PerOpNs = ElapsedNs / static_cast<double>(Iterations);
    const double Mops = (static_cast<double>(Iterations) / ElapsedSeconds) / 1e6;
    const double TotalMB = (static_cast<double>(ReusableBuffer.Num() * Iterations)) / (1024.0 * 1024.0);
    const double ThroughputMBs = TotalMB / ElapsedSeconds;

    TestEqual(TEXT("编码单帧总长度符合 16+16=32 字节"), ReusableBuffer.Num(), 32);

    AddInfo(FString::Printf(
        TEXT("【性能指标】PacketCodecEncodeTo: 迭代=%d 次, 总耗时=%.2f ms, 单次均耗=%.2f ns, 吞吐=%.2f Mops/s (%.2f MB/s)"),
        Iterations,
        ElapsedSeconds * 1000.0,
        PerOpNs,
        Mops,
        ThroughputMBs
    ));

    constexpr double BudgetNsPerOp = 200.0;
    TestTrue(
        FString::Printf(TEXT("帧就地编码单次均耗应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, PerOpNs),
        PerOpNs < BudgetNsPerOp
    );

    return true;
}

/**
 * @brief 报文头快速反解微基准性能测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetPacketCodecDecodeFastPerfTest,
    "Game.Net.Perf.PacketCodecDecodeFast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行 16 字节报文头无对象分配快速反解性能测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameNetPacketCodecDecodeFastPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetPacketHeader OriginalHeader;
    OriginalHeader.MessageId = 8888;
    OriginalHeader.SequenceId = 12345;
    OriginalHeader.PayloadLength = 64;
    const TArray<uint8> WireBytes = FNetPacketCodec::EncodeHeader(OriginalHeader);

    constexpr int32 WarmupIterations = 5000;
    FNetPacketHeader WarmupHeader;
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        FNetPacketCodec::TryDecodeHeaderFast(WireBytes, WarmupHeader);
    }

    constexpr int32 Iterations = 200000;
    FNetPacketHeader DecodedHeader;
    volatile uint32 AccumulatedMessageId = 0;

    const uint64 StartCycles = FPlatformTime::Cycles64();
    for (int32 Index = 0; Index < Iterations; ++Index) {
        if (FNetPacketCodec::TryDecodeHeaderFast(WireBytes.GetData(), WireBytes.Num(), DecodedHeader)) {
            AccumulatedMessageId = AccumulatedMessageId + DecodedHeader.MessageId;
        }
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedSeconds = FPlatformTime::ToSeconds64(ElapsedCycles);
    const double ElapsedNs = ElapsedSeconds * 1e9;
    const double PerOpNs = ElapsedNs / static_cast<double>(Iterations);
    const double Mops = (static_cast<double>(Iterations) / ElapsedSeconds) / 1e6;

    TestEqual(TEXT("反解报文头 MessageId 校验无误"), DecodedHeader.MessageId, 8888u);

    AddInfo(FString::Printf(
        TEXT("【性能指标】PacketCodecDecodeFast: 迭代=%d 次, 总耗时=%.2f ms, 单次均耗=%.2f ns, 吞吐=%.2f Mops/s"),
        Iterations,
        ElapsedSeconds * 1000.0,
        PerOpNs,
        Mops
    ));

    constexpr double BudgetNsPerOp = 50.0;
    TestTrue(
        FString::Printf(TEXT("头部快速反解单次均耗应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, PerOpNs),
        PerOpNs < BudgetNsPerOp
    );

    return true;
}

/**
 * @brief 环形缓冲区流式追加与零分配出包微基准性能测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetRingBufferConsumePerfTest,
    "Game.Net.Perf.RingBufferConsume",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行环形缓冲区追加并零拷贝消费微基准测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameNetRingBufferConsumePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetPacketHeader Header;
    Header.MessageId = 2001;
    Header.SequenceId = 1;
    const uint8 PayloadData[] = {10, 20, 30, 40, 50, 60, 70, 80};
    const TArray<uint8> EncodedFrame = FNetPacketCodec::EncodeFrame(Header, PayloadData);

    FNetPacketRingBuffer RingBuffer(8192);

    constexpr int32 WarmupIterations = 1000;
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        RingBuffer.Append(EncodedFrame);
        RingBuffer.TryConsumePacket([](const FNetPacketHeader&, TConstArrayView<uint8>) {});
    }

    constexpr int32 Iterations = 50000;
    int32 ConsumedPackets = 0;
    volatile uint32 ChecksumSink = 0;

    const uint64 StartCycles = FPlatformTime::Cycles64();
    for (int32 Index = 0; Index < Iterations; ++Index) {
        RingBuffer.Append(EncodedFrame);
        RingBuffer.TryConsumePacket([&ConsumedPackets, &ChecksumSink](const FNetPacketHeader& Hdr, TConstArrayView<uint8> Pld) {
            ++ConsumedPackets;
            ChecksumSink = ChecksumSink + Hdr.MessageId + Pld[0];
        });
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedSeconds = FPlatformTime::ToSeconds64(ElapsedCycles);
    const double ElapsedNs = ElapsedSeconds * 1e9;
    const double PerOpNs = ElapsedNs / static_cast<double>(Iterations);
    const double TotalMB = (static_cast<double>(EncodedFrame.Num() * Iterations)) / (1024.0 * 1024.0);
    const double ThroughputMBs = TotalMB / ElapsedSeconds;

    TestEqual(TEXT("消费包数与迭代次数严格一致"), ConsumedPackets, Iterations);
    TestEqual(TEXT("消费完毕后缓冲区可读字节为 0"), RingBuffer.GetReadableBytes(), 0);

    AddInfo(FString::Printf(
        TEXT("【性能指标】RingBufferFeedAndConsume: 迭代=%d 次, 总耗时=%.2f ms, 单次均耗=%.2f ns, 吞吐=%.2f MB/s"),
        Iterations,
        ElapsedSeconds * 1000.0,
        PerOpNs,
        ThroughputMBs
    ));

    constexpr double BudgetThroughputMBs = 30.0;
    TestTrue(
        FString::Printf(TEXT("环形缓冲区追加消费吞吐应高于 %.1f MB/s (实测: %.2f MB/s)"), BudgetThroughputMBs, ThroughputMBs),
        ThroughputMBs > BudgetThroughputMBs
    );

    return true;
}

/**
 * @brief UE 原生二进制 Archive 结构体序列化与反序列化往返微基准性能测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetMemoryArchiveRoundTripPerfTest,
    "Game.Net.Perf.MemoryArchiveRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行反射结构体紧凑二进制往返编解码微基准测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameNetMemoryArchiveRoundTripPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameNetTestPurchaseRsp Source = BuildPerfSamplePurchaseRsp();
    const FMemoryArchiveNetSerializer Serializer;

    constexpr int32 WarmupIterations = 500;
    TArray<uint8> WarmupBuffer;
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        Serializer.SerializeStructTo(Source, WarmupBuffer);
        FGameNetTestPurchaseRsp Decoded;
        Serializer.DeserializeStructTo(WarmupBuffer, Decoded);
    }

    constexpr int32 Iterations = 20000;
    TArray<uint8> SerializedBuffer;
    SerializedBuffer.Reserve(256);
    FGameNetTestPurchaseRsp DecodedTarget;

    const uint64 StartCycles = FPlatformTime::Cycles64();
    for (int32 Index = 0; Index < Iterations; ++Index) {
        Serializer.SerializeStructTo(Source, SerializedBuffer);
        Serializer.DeserializeStructTo(SerializedBuffer, DecodedTarget);
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedSeconds = FPlatformTime::ToSeconds64(ElapsedCycles);
    const double ElapsedUs = ElapsedSeconds * 1e6;
    const double PerOpUs = ElapsedUs / static_cast<double>(Iterations);
    const double Ops = static_cast<double>(Iterations) / ElapsedSeconds;

    TestEqual(TEXT("反序列化对象字段精确还原"), DecodedTarget.RemainingGold, Source.RemainingGold);
    TestEqual(TEXT("嵌套结构字段精确还原"), DecodedTarget.BonusItem.Quantity, Source.BonusItem.Quantity);

    AddInfo(FString::Printf(
        TEXT("【性能指标】MemoryArchiveRoundTrip: 迭代=%d 次, 总耗时=%.2f ms, 往返均耗=%.2f μs, 吞吐=%.0f ops/s, 字节数=%d"),
        Iterations,
        ElapsedSeconds * 1000.0,
        PerOpUs,
        Ops,
        SerializedBuffer.Num()
    ));

    constexpr double BudgetUsPerOp = 30.0;
    TestTrue(
        FString::Printf(TEXT("二进制 Archive 单次往返均耗应低于 %.1f μs (实测: %.2f μs)"), BudgetUsPerOp, PerOpUs),
        PerOpUs < BudgetUsPerOp
    );

    return true;
}

/**
 * @brief 紧凑 UTF-8 JSON 结构体序列化与反序列化往返微基准性能测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetJsonRoundTripPerfTest,
    "Game.Net.Perf.JsonRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行反射结构体紧凑 UTF-8 JSON 往返编解码微基准测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameNetJsonRoundTripPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameNetTestPurchaseRsp Source = BuildPerfSamplePurchaseRsp();
    const FJsonNetSerializer Serializer;

    constexpr int32 WarmupIterations = 100;
    TArray<uint8> WarmupBuffer;
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        Serializer.SerializeStructTo(Source, WarmupBuffer);
        FGameNetTestPurchaseRsp Decoded;
        Serializer.DeserializeStructTo(WarmupBuffer, Decoded);
    }

    constexpr int32 Iterations = 5000;
    TArray<uint8> JsonBuffer;
    JsonBuffer.Reserve(512);
    FGameNetTestPurchaseRsp DecodedTarget;

    const uint64 StartCycles = FPlatformTime::Cycles64();
    for (int32 Index = 0; Index < Iterations; ++Index) {
        Serializer.SerializeStructTo(Source, JsonBuffer);
        Serializer.DeserializeStructTo(JsonBuffer, DecodedTarget);
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedSeconds = FPlatformTime::ToSeconds64(ElapsedCycles);
    const double ElapsedUs = ElapsedSeconds * 1e6;
    const double PerOpUs = ElapsedUs / static_cast<double>(Iterations);
    const double Ops = static_cast<double>(Iterations) / ElapsedSeconds;

    TestEqual(TEXT("JSON 反序列化对象字段精确还原"), DecodedTarget.RemainingGold, Source.RemainingGold);
    TestEqual(TEXT("JSON 嵌套结构字段精确还原"), DecodedTarget.BonusItem.Quantity, Source.BonusItem.Quantity);

    AddInfo(FString::Printf(
        TEXT("【性能指标】JsonRoundTrip: 迭代=%d 次, 总耗时=%.2f ms, 往返均耗=%.2f μs, 吞吐=%.0f ops/s, 字节数=%d"),
        Iterations,
        ElapsedSeconds * 1000.0,
        PerOpUs,
        Ops,
        JsonBuffer.Num()
    ));

    constexpr double BudgetUsPerOp = 200.0;
    TestTrue(
        FString::Printf(TEXT("JSON 单次往返均耗应低于 %.1f μs (实测: %.2f μs)"), BudgetUsPerOp, PerOpUs),
        PerOpUs < BudgetUsPerOp
    );

    return true;
}

#endif
