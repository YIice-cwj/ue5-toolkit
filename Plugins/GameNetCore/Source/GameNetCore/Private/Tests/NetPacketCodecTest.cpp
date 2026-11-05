/**
 * @file NetPacketCodecTest.cpp
 * @brief 16 字节网络报文头与完整帧无状态二进制编解码器（FNetPacketCodec）深度单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Codec/NetPacketCodec.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetPacketCodecTest,
    "Game.Net.Core.PacketCodec",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 16 字节小端序线格式编码、反解、边界阈值与畸形头校验
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetPacketCodecTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetPacketHeader Original;
    Original.Magic = FNetPacketHeader::PACKET_MAGIC;
    Original.HeaderSize = FNetPacketHeader::WIRE_HEADER_BYTES;
    Original.PayloadLength = 1024;
    Original.MessageId = 0x01020304u;
    Original.SequenceId = 0xAABBCCDDu;

    TestTrue(TEXT("默认合法报文头应通过校验"), FNetPacketCodec::IsValidHeader(Original));
    TestEqual(
        TEXT("合法报文头状态为 CompleteFrame"),
        FNetPacketCodec::ValidateHeader(Original),
        ENetFrameParseStatus::CompleteFrame
    );
    TestTrue(TEXT("压线等于最大允许负载阈值应放行"), FNetPacketCodec::IsValidHeader(Original, 1024));
    TestFalse(TEXT("超过最大允许负载阈值应被拦截"), FNetPacketCodec::IsValidHeader(Original, 1023));
    TestEqual(
        TEXT("超限负载返回 PayloadExceedsLimit"),
        FNetPacketCodec::ValidateHeader(Original, 1023),
        ENetFrameParseStatus::PayloadExceedsLimit
    );

    FNetPacketHeader BadMagic = Original;
    BadMagic.Magic = 0x1234;
    TestEqual(
        TEXT("错误魔数返回 InvalidMagic"),
        FNetPacketCodec::ValidateHeader(BadMagic),
        ENetFrameParseStatus::InvalidMagic
    );
    TestFalse(TEXT("错误魔数 IsValidHeader 为 false"), FNetPacketCodec::IsValidHeader(BadMagic));

    FNetPacketHeader BadSize = Original;
    BadSize.HeaderSize = 20;
    TestEqual(
        TEXT("错误头部长度返回 InvalidHeaderSize"),
        FNetPacketCodec::ValidateHeader(BadSize),
        ENetFrameParseStatus::InvalidHeaderSize
    );

    const TArray<uint8> WireHeader = FNetPacketCodec::EncodeHeader(Original);
    TestEqual(TEXT("线格式报文头固定为 16 字节"), WireHeader.Num(), 16);
    TestEqual(TEXT("小端序 Magic 低字节为 0x45 ('E')"), WireHeader[0], static_cast<uint8>(0x45));
    TestEqual(TEXT("小端序 Magic 高字节为 0x55 ('U')"), WireHeader[1], static_cast<uint8>(0x55));
    TestEqual(TEXT("小端序 MessageId 首字节为 0x04"), WireHeader[8], static_cast<uint8>(0x04));
    TestEqual(TEXT("小端序 MessageId 末字节为 0x01"), WireHeader[11], static_cast<uint8>(0x01));

    TestFalse(TEXT("空切片反解报文头返回空值"), FNetPacketCodec::TryDecodeHeader({}).IsSet());
    TestFalse(
        TEXT("不足 16 字节切片反解返回空值"),
        FNetPacketCodec::TryDecodeHeader(TConstArrayView<uint8>(WireHeader.GetData(), 15)).IsSet()
    );

    const TOptional<FNetPacketHeader> Decoded = FNetPacketCodec::TryDecodeHeader(WireHeader);
    TestTrue(TEXT("完整 16 字节切片成功反解"), Decoded.IsSet());
    if (!Decoded.IsSet()) {
        return false;
    }
    TestTrue(TEXT("反解后的报文头与原始结构严格相等"), *Decoded == Original);

    const TArray<uint8> EmptyFrame = FNetPacketCodec::EncodeFrame(Original, {});
    TestEqual(TEXT("空负载帧总长度为 16 字节"), EmptyFrame.Num(), 16);
    const TOptional<FNetPacketHeader> EmptyDecoded = FNetPacketCodec::TryDecodeHeader(EmptyFrame);
    TestTrue(TEXT("空负载帧头自动归一化 PayloadLength 为 0"), EmptyDecoded.IsSet() && EmptyDecoded->PayloadLength == 0);

    const uint8 SamplePayload[] = {10, 20, 30, 40, 50};
    const TArray<uint8> FullFrame = FNetPacketCodec::EncodeFrame(Original, SamplePayload);
    TestEqual(TEXT("带 5 字节负载的完整帧长度为 21"), FullFrame.Num(), 21);
    const TOptional<FNetPacketHeader> FullDecoded = FNetPacketCodec::TryDecodeHeader(FullFrame);
    TestTrue(TEXT("带负载帧头 PayloadLength 归一化为 5"), FullDecoded.IsSet() && FullDecoded->PayloadLength == 5);

    return true;
}

#endif

