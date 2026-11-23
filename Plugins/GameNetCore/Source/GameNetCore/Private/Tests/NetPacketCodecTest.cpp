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
 * @brief 验证 16 字节小端序线格式就地编码、反解、边界阈值与畸形头校验
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

    uint8 StackHeaderBytes[FNetPacketHeader::WIRE_HEADER_BYTES];
    FNetPacketCodec::EncodeHeaderTo(Original, StackHeaderBytes);
    TestEqual(TEXT("小端序 Magic 低字节为 0x45 ('E')"), StackHeaderBytes[0], static_cast<uint8>(0x45));
    TestEqual(TEXT("小端序 Magic 高字节为 0x55 ('U')"), StackHeaderBytes[1], static_cast<uint8>(0x55));
    TestEqual(TEXT("小端序 MessageId 首字节为 0x04"), StackHeaderBytes[8], static_cast<uint8>(0x04));
    TestEqual(TEXT("小端序 MessageId 末字节为 0x01"), StackHeaderBytes[11], static_cast<uint8>(0x01));

    FNetPacketHeader Decoded;
    TestFalse(TEXT("空切片反解报文头返回 false"), FNetPacketCodec::TryDecodeHeader({}, Decoded));
    TestFalse(
        TEXT("不足 16 字节切片反解返回 false"),
        FNetPacketCodec::TryDecodeHeader(TConstArrayView<uint8>(StackHeaderBytes, 15), Decoded)
    );
    TestTrue(TEXT("完整 16 字节切片成功反解"), FNetPacketCodec::TryDecodeHeader(StackHeaderBytes, Decoded));
    TestTrue(TEXT("反解后的报文头与原始结构严格相等"), Decoded == Original);

    TArray<uint8> EmptyFrame;
    FNetPacketCodec::EncodeFrameTo(Original, {}, EmptyFrame);
    TestEqual(TEXT("空负载帧总长度为 16 字节"), EmptyFrame.Num(), 16);
    FNetPacketHeader EmptyDecoded;
    TestTrue(TEXT("空负载帧头反解成功"), FNetPacketCodec::TryDecodeHeader(EmptyFrame, EmptyDecoded));
    TestEqual(TEXT("空负载帧头自动归一化 PayloadLength 为 0"), EmptyDecoded.PayloadLength, 0u);

    const uint8 SamplePayload[] = {10, 20, 30, 40, 50};
    TArray<uint8> FullFrame;
    FullFrame.Reserve(64);
    FNetPacketCodec::EncodeFrameTo(Original, SamplePayload, FullFrame);
    TestEqual(TEXT("带 5 字节负载的完整帧长度为 21"), FullFrame.Num(), 21);

    FNetPacketHeader FullDecoded;
    TestTrue(TEXT("带负载帧头反解成功"), FNetPacketCodec::TryDecodeHeader(FullFrame, FullDecoded));
    TestEqual(TEXT("带负载帧头 PayloadLength 归一化为 5"), FullDecoded.PayloadLength, 5u);
    TestEqual(TEXT("带负载帧负载末字节一致"), FullFrame[20], static_cast<uint8>(50));

    FNetPacket ValuePacket(Original, SamplePayload);
    TestEqual(TEXT("带参构造 FNetPacket 报文头一致"), ValuePacket.Header, Original);
    TestEqual(TEXT("带参构造 FNetPacket 负载字节一致"), ValuePacket.Payload.Num(), 5);
    FNetPacket PacketCopy = ValuePacket;
    TestTrue(TEXT("FNetPacket 相等性运算符一致"), PacketCopy == ValuePacket);
    PacketCopy.Reset();
    TestEqual(TEXT("FNetPacket 重置后负载为空"), PacketCopy.Payload.Num(), 0);
    TestFalse(TEXT("FNetPacket 重置后与原包不相等"), PacketCopy == ValuePacket);

    return true;
}

#endif
