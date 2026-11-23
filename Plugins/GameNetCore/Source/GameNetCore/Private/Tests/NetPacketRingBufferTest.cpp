/**
 * @file NetPacketRingBufferTest.cpp
 * @brief 环形字节流帧解析器（FNetPacketRingBuffer）2 的幂对齐、跨环回边界、溢出熔断与重入安全单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Codec/NetPacketCodec.h"
#include "Codec/NetPacketRingBuffer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetPacketRingBufferTest,
    "Game.Net.Core.RingBuffer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证环形流解析器在半包、粘包、跨界环回重组、畸形帧熔断、溢出熔断与回调重入下的确定性行为
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetPacketRingBufferTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetPacketRingBuffer RingBuffer(16, 256);
    TestEqual(TEXT("低于最小容量阈值时自动钳制到 64 字节（2 的幂）"), RingBuffer.GetCapacityBytes(), 64);
    TestEqual(TEXT("最大单包负载上限符合构造实参"), RingBuffer.GetMaxPayloadBytes(), 256u);
    TestEqual(TEXT("初始可读字节为 0"), RingBuffer.GetReadableBytes(), 0);
    TestTrue(TEXT("最大缓冲容量上限为合法 2 的幂"), RingBuffer.GetMaxBufferCapacityBytes() >= 544);

    FNetPacketRingBuffer NonPow2Buffer(100, 256);
    TestEqual(TEXT("非 2 的幂初始容量 100 自动向上规整为 128"), NonPow2Buffer.GetCapacityBytes(), 128);

    TestTrue(TEXT("追加空切片返回 true"), RingBuffer.Append({}));
    TestEqual(TEXT("追加空切片不改变可读字节"), RingBuffer.GetReadableBytes(), 0);
    TestEqual(
        TEXT("空缓冲区消费返回 NeedMoreData"),
        RingBuffer.TryConsumePacket([](const FNetPacketHeader&, TConstArrayView<uint8>) {}),
        ENetFrameParseStatus::NeedMoreData
    );

    FNetPacketHeader HeaderA;
    HeaderA.MessageId = 101;
    HeaderA.SequenceId = 1;
    const uint8 PayloadA[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    TArray<uint8> FrameA;
    FNetPacketCodec::EncodeFrameTo(HeaderA, PayloadA, FrameA);

    FNetPacketHeader HeaderB;
    HeaderB.MessageId = 202;
    HeaderB.SequenceId = 2;
    TArray<uint8> FrameB;
    FNetPacketCodec::EncodeFrameTo(HeaderB, {}, FrameB);

    FNetPacketHeader HeaderC;
    HeaderC.MessageId = 303;
    HeaderC.SequenceId = 3;
    TArray<uint8> PayloadC;
    for (int32 Index = 0; Index < 120; ++Index) {
        PayloadC.Add(static_cast<uint8>(Index & 0xFF));
    }
    TArray<uint8> FrameC;
    FNetPacketCodec::EncodeFrameTo(HeaderC, PayloadC, FrameC);

    for (int32 ByteIdx = 0; ByteIdx < FrameA.Num() - 1; ++ByteIdx) {
        TestTrue(TEXT("逐字节追加半包成功"), RingBuffer.Append(TConstArrayView<uint8>(FrameA.GetData() + ByteIdx, 1)));
        TestEqual(
            TEXT("半包未收齐前始终返回 NeedMoreData"),
            RingBuffer.TryConsumePacket([](const FNetPacketHeader&, TConstArrayView<uint8>) {}),
            ENetFrameParseStatus::NeedMoreData
        );
    }
    TestTrue(TEXT("追加末字节成功"), RingBuffer.Append(TConstArrayView<uint8>(FrameA.GetData() + FrameA.Num() - 1, 1)));

    uint32 CapturedMsgId = 0;
    TArray<uint8> CapturedPayload;
    const ENetFrameParseStatus StatusA = RingBuffer.TryConsumePacket(
        [&CapturedMsgId, &CapturedPayload](const FNetPacketHeader& Hdr, TConstArrayView<uint8> Pld) {
            CapturedMsgId = Hdr.MessageId;
            CapturedPayload.Reset();
            CapturedPayload.Append(Pld.GetData(), Pld.Num());
        }
    );
    TestEqual(TEXT("收齐末字节后成功消费 FrameA"), StatusA, ENetFrameParseStatus::CompleteFrame);
    TestEqual(TEXT("FrameA MessageId 精确一致"), CapturedMsgId, 101u);
    TestEqual(TEXT("FrameA 负载长度一致"), CapturedPayload.Num(), 20);
    TestEqual(TEXT("FrameA 末字节内容一致"), CapturedPayload[19], static_cast<uint8>(20));

    FNetPacketRingBuffer WrapRing(64, 256);
    TestTrue(TEXT("先写入 48 字节预置包制造非零游标"), WrapRing.Append(FrameA));
    TestTrue(TEXT("追加半包 B 头部推动偏移"), WrapRing.Append(TConstArrayView<uint8>(FrameB.GetData(), 12)));
    TestEqual(
        TEXT("消费预置 FrameA 使 HeadIndex 停在 36"),
        WrapRing.TryConsumePacket([](const FNetPacketHeader&, TConstArrayView<uint8>) {}),
        ENetFrameParseStatus::CompleteFrame
    );
    TestTrue(TEXT("补全 FrameB 剩余 4 字节"), WrapRing.Append(TConstArrayView<uint8>(FrameB.GetData() + 12, 4)));
    TestTrue(TEXT("在 64B 容量下追加跨环回边界的 FrameA（跨越 64B 尾部回绕至 0）"), WrapRing.Append(FrameA));
    TestEqual(TEXT("容量仍保持 64 字节触发真实跨界环回"), WrapRing.GetCapacityBytes(), 64);

    TArray<uint32> WrapIds;
    TArray<uint8> WrapPayload;
    int32 WrapConsumedCount = 0;
    const ENetFrameParseStatus WrapTerminal = WrapRing.ConsumeAllAvailable(
        [&WrapIds, &WrapPayload](const FNetPacketHeader& Hdr, TConstArrayView<uint8> Pld) {
            WrapIds.Add(Hdr.MessageId);
            if (Hdr.MessageId == 101u) {
                WrapPayload.Reset();
                WrapPayload.Append(Pld.GetData(), Pld.Num());
            }
        },
        &WrapConsumedCount
    );
    TestEqual(TEXT("跨界批量消费正常排空返回 NeedMoreData"), WrapTerminal, ENetFrameParseStatus::NeedMoreData);
    TestEqual(TEXT("跨界批量消费产出 2 帧"), WrapConsumedCount, 2);
    TestEqual(TEXT("首帧为 202"), WrapIds[0], 202u);
    TestEqual(TEXT("次帧跨界重组 101 负载完整无损"), WrapPayload.Num(), 20);
    TestEqual(TEXT("跨界重组末字节精确一致"), WrapPayload[19], static_cast<uint8>(20));

    TestTrue(TEXT("追加大包触发 2 的幂线性化扩容"), RingBuffer.Append(FrameA));
    TestTrue(TEXT("紧接着粘包追加 FrameC"), RingBuffer.Append(FrameC));
    TestTrue(TEXT("扩容后容量为 2 的幂且足够容纳"), RingBuffer.GetCapacityBytes() >= 256);

    int32 StickyCount = 0;
    TArray<uint8> PoppedPayloadC;
    const ENetFrameParseStatus StickyStatus = RingBuffer.ConsumeAllAvailable(
        [&PoppedPayloadC](const FNetPacketHeader& Hdr, TConstArrayView<uint8> Pld) {
            if (Hdr.MessageId == 303u) {
                PoppedPayloadC.Reset();
                PoppedPayloadC.Append(Pld.GetData(), Pld.Num());
            }
        },
        &StickyCount
    );
    TestEqual(TEXT("粘包批量消费正常排空"), StickyStatus, ENetFrameParseStatus::NeedMoreData);
    TestEqual(TEXT("累计消费 2 帧"), StickyCount, 2);
    TestEqual(TEXT("FrameC 负载无损一致"), PoppedPayloadC, PayloadC);
    TestEqual(TEXT("全部消费完毕后可读字节归零"), RingBuffer.GetReadableBytes(), 0);

    uint8 BadHeaderBytes[FNetPacketHeader::WIRE_HEADER_BYTES];
    FNetPacketHeader OversizedHeader;
    OversizedHeader.PayloadLength = 9999;
    FNetPacketCodec::EncodeHeaderTo(OversizedHeader, BadHeaderBytes);
    TestTrue(TEXT("追加超限头成功"), RingBuffer.Append(BadHeaderBytes));
    TestEqual(
        TEXT("超限负载头立即返回 PayloadExceedsLimit 熔断"),
        RingBuffer.TryConsumePacket([](const FNetPacketHeader&, TConstArrayView<uint8>) {}),
        ENetFrameParseStatus::PayloadExceedsLimit
    );
    TestEqual(TEXT("超限熔断后清空缓冲区"), RingBuffer.GetReadableBytes(), 0);

    FNetPacketHeader BadSizeHeader;
    BadSizeHeader.HeaderSize = 24;
    FNetPacketCodec::EncodeHeaderTo(BadSizeHeader, BadHeaderBytes);
    TestTrue(TEXT("追加错长度头成功"), RingBuffer.Append(BadHeaderBytes));
    TestEqual(
        TEXT("非法 HeaderSize 返回 InvalidHeaderSize 熔断"),
        RingBuffer.TryConsumePacket([](const FNetPacketHeader&, TConstArrayView<uint8>) {}),
        ENetFrameParseStatus::InvalidHeaderSize
    );
    TestEqual(TEXT("非法 HeaderSize 熔断后清空缓冲区"), RingBuffer.GetReadableBytes(), 0);

    FNetPacketHeader CorruptMagicHeader;
    CorruptMagicHeader.Magic = 0xDEAD;
    FNetPacketCodec::EncodeHeaderTo(CorruptMagicHeader, BadHeaderBytes);
    TestTrue(TEXT("追加错魔数头成功"), RingBuffer.Append(BadHeaderBytes));
    int32 BadConsumed = -1;
    TestEqual(
        TEXT("ConsumeAllAvailable 遇错魔数返回精确 InvalidMagic 终态"),
        RingBuffer.ConsumeAllAvailable([](const FNetPacketHeader&, TConstArrayView<uint8>) {}, &BadConsumed),
        ENetFrameParseStatus::InvalidMagic
    );
    TestEqual(TEXT("错魔数熔断消耗帧数为 0"), BadConsumed, 0);

    TArray<uint8> HugeStream;
    HugeStream.SetNumZeroed(RingBuffer.GetMaxBufferCapacityBytes() + 1);
    TestFalse(TEXT("超过最大安全缓冲容量的恶意追加立即熔断返回 false"), RingBuffer.Append(HugeStream));
    TestEqual(TEXT("容量熔断后可读字节归零"), RingBuffer.GetReadableBytes(), 0);

    TestTrue(TEXT("写入正常包准备验证回调内重入 Reset"), RingBuffer.Append(FrameA));
    const ENetFrameParseStatus ReentrantStatus = RingBuffer.TryConsumePacket(
        [&RingBuffer](const FNetPacketHeader&, TConstArrayView<uint8>) {
            RingBuffer.Reset();
        }
    );
    TestEqual(TEXT("回调内重入 Reset 时本次消费返回 CompleteFrame"), ReentrantStatus, ENetFrameParseStatus::CompleteFrame);
    TestEqual(TEXT("回调内重入 Reset 后 ReadableBytes 保持 0 绝不变成负数"), RingBuffer.GetReadableBytes(), 0);

    FNetPacketRingBuffer LargeScratchRing(64 * 1024, 128 * 1024);
    TArray<uint8> BigPayload;
    BigPayload.SetNumZeroed(70 * 1024);
    FNetPacketHeader BigHdr;
    BigHdr.MessageId = 777;
    TArray<uint8> BigFrame;
    FNetPacketCodec::EncodeFrameTo(BigHdr, BigPayload, BigFrame);
    TestTrue(TEXT("推入前置偏移包"), LargeScratchRing.Append(FrameA));
    TestTrue(TEXT("推入 70KB 大包使其跨界"), LargeScratchRing.Append(BigFrame));
    TestEqual(
        TEXT("消费前置小包"),
        LargeScratchRing.TryConsumePacket([](const FNetPacketHeader&, TConstArrayView<uint8>) {}),
        ENetFrameParseStatus::CompleteFrame
    );
    TestEqual(
        TEXT("消费 70KB 大包"),
        LargeScratchRing.TryConsumePacket([](const FNetPacketHeader&, TConstArrayView<uint8>) {}),
        ENetFrameParseStatus::CompleteFrame
    );
    LargeScratchRing.Reset();
    TestEqual(TEXT("超大重组缓冲 Reset 后可读字节归零"), LargeScratchRing.GetReadableBytes(), 0);

    return true;
}

#endif
