// Copyright Epic Games, Inc. All Rights Reserved.

#include "Codec/NetPacketCodec.h"

#include "HAL/UnrealMemory.h"
#include "Misc/ByteSwap.h"

namespace {

/**
 * @brief 将 16 位无符号整数按小端序写入目标内存
 * @param Destination 目标字节首地址
 * @param Value 待写入数值
 */
FORCEINLINE void WriteLittleEndianUint16(uint8* Destination, uint16 Value) noexcept {
    const uint16 LittleEndianValue = INTEL_ORDER16(Value);
    FMemory::Memcpy(Destination, &LittleEndianValue, sizeof(uint16));
}

/**
 * @brief 将 32 位无符号整数按小端序写入目标内存
 * @param Destination 目标字节首地址
 * @param Value 待写入数值
 */
FORCEINLINE void WriteLittleEndianUint32(uint8* Destination, uint32 Value) noexcept {
    const uint32 LittleEndianValue = INTEL_ORDER32(Value);
    FMemory::Memcpy(Destination, &LittleEndianValue, sizeof(uint32));
}

/**
 * @brief 从源内存按小端序读取 16 位无符号整数
 * @param Source 源字节首地址
 * @return 主机字节序 16 位无符号整数
 */
[[nodiscard]] FORCEINLINE uint16 ReadLittleEndianUint16(const uint8* Source) noexcept {
    uint16 RawValue = 0;
    FMemory::Memcpy(&RawValue, Source, sizeof(uint16));
    return INTEL_ORDER16(RawValue);
}

/**
 * @brief 从源内存按小端序读取 32 位无符号整数
 * @param Source 源字节首地址
 * @return 主机字节序 32 位无符号整数
 */
[[nodiscard]] FORCEINLINE uint32 ReadLittleEndianUint32(const uint8* Source) noexcept {
    uint32 RawValue = 0;
    FMemory::Memcpy(&RawValue, Source, sizeof(uint32));
    return INTEL_ORDER32(RawValue);
}

}  // namespace

/**
 * @brief 校验报文头魔数、头部尺寸与负载长度上限，返回精确解析状态码
 * @param Header 待校验的报文头
 * @param MaxAllowedPayloadBytes 单包允许的最大负载字节数上限
 * @return 校验通过返回 CompleteFrame，否则返回对应违例枚举
 */
ENetFrameParseStatus FNetPacketCodec::ValidateHeader(
    const FNetPacketHeader& Header,
    uint32 MaxAllowedPayloadBytes
) noexcept {
    if (Header.Magic != FNetPacketHeader::PACKET_MAGIC) {
        return ENetFrameParseStatus::InvalidMagic;
    }
    if (Header.HeaderSize != FNetPacketHeader::WIRE_HEADER_BYTES) {
        return ENetFrameParseStatus::InvalidHeaderSize;
    }
    if (Header.PayloadLength > MaxAllowedPayloadBytes) {
        return ENetFrameParseStatus::PayloadExceedsLimit;
    }
    return ENetFrameParseStatus::CompleteFrame;
}

/**
 * @brief 判定报文头是否通过魔数、头部尺寸与负载长度校验
 * @param Header 待校验的报文头
 * @param MaxAllowedPayloadBytes 单包允许的最大负载字节数上限
 * @return 合法返回 true，否则返回 false
 */
bool FNetPacketCodec::IsValidHeader(const FNetPacketHeader& Header, uint32 MaxAllowedPayloadBytes) noexcept {
    return ValidateHeader(Header, MaxAllowedPayloadBytes) == ENetFrameParseStatus::CompleteFrame;
}

/**
 * @brief 将报文头按小端序就地编码写入目标字节缓冲区首地址
 * @param Header 待编码的报文头
 * @param OutWireBytes 目标写入字节缓冲区首地址
 */
void FNetPacketCodec::EncodeHeaderTo(const FNetPacketHeader& Header, uint8* OutWireBytes) noexcept {
    WriteLittleEndianUint16(OutWireBytes, Header.Magic);
    WriteLittleEndianUint16(OutWireBytes + 2, Header.HeaderSize);
    WriteLittleEndianUint32(OutWireBytes + 4, Header.PayloadLength);
    WriteLittleEndianUint32(OutWireBytes + 8, Header.MessageId);
    WriteLittleEndianUint32(OutWireBytes + 12, Header.SequenceId);
}

/**
 * @brief 将单帧报文头与负载直接编码并写入调用方提供的复用缓冲区
 * @param Header 待编码的报文头模板
 * @param Payload 负载二进制切片
 * @param OutFrameBytes 目标写入字节数组
 */
void FNetPacketCodec::EncodeFrameTo(
    const FNetPacketHeader& Header,
    TConstArrayView<uint8> Payload,
    TArray<uint8>& OutFrameBytes
) {
    const int32 PayloadSize = Payload.Num();
    const int32 TotalFrameBytes = FNetPacketHeader::WIRE_HEADER_BYTES + PayloadSize;

    OutFrameBytes.SetNumUninitialized(TotalFrameBytes);
    uint8* Destination = OutFrameBytes.GetData();

    FNetPacketHeader EffectiveHeader = Header;
    EffectiveHeader.PayloadLength = static_cast<uint32>(PayloadSize);
    EncodeHeaderTo(EffectiveHeader, Destination);

    if (PayloadSize > 0) {
        FMemory::Memcpy(Destination + FNetPacketHeader::WIRE_HEADER_BYTES, Payload.GetData(), PayloadSize);
    }
}

/**
 * @brief 从二进制切片首部零分配高速反解 16 字节小端序报文头
 * @param WireBytes 原始字节切片
 * @param OutHeader 反解成功时写入的目标报文头引用
 * @return 字节数达到 16 且首地址有效时返回 true，否则返回 false
 */
bool FNetPacketCodec::TryDecodeHeader(TConstArrayView<uint8> WireBytes, FNetPacketHeader& OutHeader) noexcept {
    const uint8* Cursor = WireBytes.GetData();
    if (Cursor == nullptr || WireBytes.Num() < FNetPacketHeader::WIRE_HEADER_BYTES) {
        return false;
    }

    OutHeader.Magic = ReadLittleEndianUint16(Cursor);
    OutHeader.HeaderSize = ReadLittleEndianUint16(Cursor + 2);
    OutHeader.PayloadLength = ReadLittleEndianUint32(Cursor + 4);
    OutHeader.MessageId = ReadLittleEndianUint32(Cursor + 8);
    OutHeader.SequenceId = ReadLittleEndianUint32(Cursor + 12);
    return true;
}
