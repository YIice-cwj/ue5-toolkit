/**
 * @file NetPacketRingBufferTest.cpp
 * @brief 环形字节流帧解析器（FNetPacketRingBuffer）粘包、半包、跨界扩容与畸形帧熔断单元测试
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
 * @brief 验证环形流解析器在逐字节半包、多帧粘包、环形跨界扩容与恶意包熔断下的确定性行为
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetPacketRingBufferTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetPacketRingBuffer RingBuffer(16, 256);
    TestEqual(TEXT("低于最小容量阈值时自动钳制到 64 字节"), RingBuffer.GetCapacityBytes(), 64);
    TestEqual(TEXT("最大单包负载上限符合构造实参"), RingBuffer.GetMaxPayloadBytes(), 256u);
    TestEqual(TEXT("初始可读字节为 0"), RingBuffer.GetReadableBytes(), 0);

    RingBuffer.Append({});
    TestEqual(TEXT("追加空切片不改变可读字节"), RingBuffer.GetReadableBytes(), 0);
    TestEqual(TEXT("空缓冲区弹出返回 NeedMoreData"), RingBuffer.TryPopPacket().Status, ENetFrameParseStatus::NeedMoreData);

    FNetPacketHeader HeaderA;
    HeaderA.MessageId = 101;
    HeaderA.SequenceId = 1;
    const uint8 PayloadA[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    const TArray<uint8> FrameA = FNetPacketCodec::EncodeFrame(HeaderA, PayloadA);

    FNetPacketHeader HeaderB;
    HeaderB.MessageId = 202;
    HeaderB.SequenceId = 2;
    const TArray<uint8> FrameB = FNetPacketCodec::EncodeFrame(HeaderB, {});

    FNetPacketHeader HeaderC;
    HeaderC.MessageId = 303;
    HeaderC.SequenceId = 3;
    TArray<uint8> PayloadC;
    for (int32 Index = 0; Index < 120; ++Index) {
        PayloadC.Add(static_cast<uint8>(Index & 0xFF));
    }
    const TArray<uint8> FrameC = FNetPacketCodec::EncodeFrame(HeaderC, PayloadC);

    for (int32 ByteIdx = 0; ByteIdx < FrameA.Num() - 1; ++ByteIdx) {
        RingBuffer.Append(TConstArrayView<uint8>(FrameA.GetData() + ByteIdx, 1));
        TestEqual(
            TEXT("半包未收齐前始终返回 NeedMoreData"),
            RingBuffer.TryPopPacket().Status,
            ENetFrameParseStatus::NeedMoreData
        );
    }
    RingBuffer.Append(TConstArrayView<uint8>(FrameA.GetData() + FrameA.Num() - 1, 1));
    const FNetFrameDecodeOutcome PoppedA = RingBuffer.TryPopPacket();
    TestEqual(TEXT("收齐末字节后成功解出 FrameA"), PoppedA.Status, ENetFrameParseStatus::CompleteFrame);
    TestEqual(TEXT("FrameA MessageId 精确一致"), PoppedA.Packet.Header.MessageId, 101u);
    TestEqual(TEXT("FrameA 负载长度一致"), PoppedA.Packet.Payload.Num(), 20);
    TestEqual(TEXT("FrameA 末字节内容一致"), PoppedA.Packet.Payload[19], static_cast<uint8>(20));

    RingBuffer.Append(FrameA);
    RingBuffer.Append(TConstArrayView<uint8>(FrameB.GetData(), 10));
    const FNetFrameDecodeOutcome FirstSticky = RingBuffer.TryPopPacket();
    TestEqual(TEXT("消费前序包使 HeadIndex 处于非零中间偏移"), FirstSticky.Status, ENetFrameParseStatus::CompleteFrame);
    TestEqual(TEXT("剩余半个 FrameB 头部为 10 字节"), RingBuffer.GetReadableBytes(), 10);

    RingBuffer.Append(TConstArrayView<uint8>(FrameB.GetData() + 10, FrameB.Num() - 10));
    RingBuffer.Append(FrameC);
    TestTrue(TEXT("跨环形边界追加大包后自动倍增扩容"), RingBuffer.GetCapacityBytes() >= 152);

    const FNetFrameDecodeOutcome PoppedB = RingBuffer.TryPopPacket();
    TestEqual(TEXT("扩容线性化后完整解出零负载 FrameB"), PoppedB.Status, ENetFrameParseStatus::CompleteFrame);
    TestEqual(TEXT("FrameB MessageId 为 202"), PoppedB.Packet.Header.MessageId, 202u);
    TestEqual(TEXT("FrameB 负载为空"), PoppedB.Packet.Payload.Num(), 0);

    const FNetFrameDecodeOutcome PoppedC = RingBuffer.TryPopPacket();
    TestEqual(TEXT("紧接着完整解出粘包 FrameC"), PoppedC.Status, ENetFrameParseStatus::CompleteFrame);
    TestEqual(TEXT("FrameC SequenceId 为 3"), PoppedC.Packet.Header.SequenceId, 3u);
    TestEqual(TEXT("FrameC 负载为 120 字节且内容无错位"), PoppedC.Packet.Payload, PayloadC);
    TestEqual(TEXT("全部消费完毕后可读字节归零"), RingBuffer.GetReadableBytes(), 0);

    FNetPacketHeader OversizedHeader;
    OversizedHeader.PayloadLength = 9999;
    OversizedHeader.MessageId = 404;
    RingBuffer.Append(FNetPacketCodec::EncodeHeader(OversizedHeader));
    const FNetFrameDecodeOutcome OversizedOutcome = RingBuffer.TryPopPacket();
    TestEqual(
        TEXT("超出单包阈值的畸形头立即触发 PayloadExceedsLimit 熔断"),
        OversizedOutcome.Status,
        ENetFrameParseStatus::PayloadExceedsLimit
    );
    TestEqual(TEXT("熔断后自动清空污染缓冲区"), RingBuffer.GetReadableBytes(), 0);

    FNetPacketHeader CorruptMagicHeader;
    CorruptMagicHeader.Magic = 0xDEAD;
    RingBuffer.Append(FNetPacketCodec::EncodeHeader(CorruptMagicHeader));
    TestEqual(
        TEXT("错误魔数流立即触发 InvalidMagic 熔断"),
        RingBuffer.TryPopPacket().Status,
        ENetFrameParseStatus::InvalidMagic
    );
    TestEqual(TEXT("错误魔数熔断后可读字节归零"), RingBuffer.GetReadableBytes(), 0);

    return true;
}

#endif

