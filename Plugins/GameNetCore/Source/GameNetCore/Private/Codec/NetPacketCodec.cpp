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
void WriteLittleEndianUint16(uint8* Destination, uint16 Value) noexcept {
    const uint16 LittleEndianValue = INTEL_ORDER16(Value);
    FMemory::Memcpy(Destination, &LittleEndianValue, sizeof(uint16));
}

/**
 * @brief 将 32 位无符号整数按小端序写入目标内存
 * @param Destination 目标字节首地址
 * @param Value 待写入数值
 */
void WriteLittleEndianUint32(uint8* Destination, uint32 Value) noexcept {
    const uint32 LittleEndianValue = INTEL_ORDER32(Value);
    FMemory::Memcpy(Destination, &LittleEndianValue, sizeof(uint32));
}

/**
 * @brief 从源内存按小端序读取 16 位无符号整数
 * @param Source 源字节首地址
 * @return 主机字节序 16 位无符号整数
 */
uint16 ReadLittleEndianUint16(const uint8* Source) noexcept {
    uint16 RawValue = 0;
    FMemory::Memcpy(&RawValue, Source, sizeof(uint16));
    return INTEL_ORDER16(RawValue);
}

/**
 * @brief 从源内存按小端序读取 32 位无符号整数
 * @param Source 源字节首地址
 * @return 主机字节序 32 位无符号整数
 */
uint32 ReadLittleEndianUint32(const uint8* Source) noexcept {
    uint32 RawValue = 0;
    FMemory::Memcpy(&RawValue, Source, sizeof(uint32));
    return INTEL_ORDER32(RawValue);
}

}

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
 * @brief 将报文头编码为跨平台一致的 16 字节小端序二进制数组
 * @param Header 待编码的报文头
 * @return 长度为 16 字节的线格式数组
 */
TArray<uint8> FNetPacketCodec::EncodeHeader(const FNetPacketHeader& Header) {
    TArray<uint8> WireBytes;
    WireBytes.SetNumUninitialized(FNetPacketHeader::WIRE_HEADER_BYTES);
    uint8* Cursor = WireBytes.GetData();
    WriteLittleEndianUint16(Cursor, Header.Magic);
    WriteLittleEndianUint16(Cursor + 2, Header.HeaderSize);
    WriteLittleEndianUint32(Cursor + 4, Header.PayloadLength);
    WriteLittleEndianUint32(Cursor + 8, Header.MessageId);
    WriteLittleEndianUint32(Cursor + 12, Header.SequenceId);
    return WireBytes;
}

/**
 * @brief 将报文头与负载拼装为单帧完整线格式字节数组（自动对齐 PayloadLength 并就地编码，消除临时堆分配）
 * @param Header 待编码的报文头模板
 * @param Payload 负载二进制切片
 * @return 包含 16 字节头与完整负载的连续字节数组
 */
TArray<uint8> FNetPacketCodec::EncodeFrame(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload) {
    const int32 PayloadSize = Payload.Num();
    const int32 TotalFrameBytes = FNetPacketHeader::WIRE_HEADER_BYTES + PayloadSize;

    TArray<uint8> FrameBytes;
    FrameBytes.SetNumUninitialized(TotalFrameBytes);
    uint8* Destination = FrameBytes.GetData();

    WriteLittleEndianUint16(Destination, Header.Magic);
    WriteLittleEndianUint16(Destination + 2, Header.HeaderSize);
    WriteLittleEndianUint32(Destination + 4, static_cast<uint32>(PayloadSize));
    WriteLittleEndianUint32(Destination + 8, Header.MessageId);
    WriteLittleEndianUint32(Destination + 12, Header.SequenceId);

    if (PayloadSize > 0) {
        FMemory::Memcpy(Destination + FNetPacketHeader::WIRE_HEADER_BYTES, Payload.GetData(), PayloadSize);
    }
    return FrameBytes;
}

/**
 * @brief 从二进制切片首部反解 16 字节小端序报文头
 * @param WireBytes 原始字节视图
 * @return 字节数达到 16 时返回解析出的报文头，不足 16 字节时返回空值
 */
TOptional<FNetPacketHeader> FNetPacketCodec::TryDecodeHeader(TConstArrayView<uint8> WireBytes) {
    if (WireBytes.Num() < FNetPacketHeader::WIRE_HEADER_BYTES) {
        return NullOpt;
    }

    const uint8* Cursor = WireBytes.GetData();
    FNetPacketHeader Decoded;
    Decoded.Magic = ReadLittleEndianUint16(Cursor);
    Decoded.HeaderSize = ReadLittleEndianUint16(Cursor + 2);
    Decoded.PayloadLength = ReadLittleEndianUint32(Cursor + 4);
    Decoded.MessageId = ReadLittleEndianUint32(Cursor + 8);
    Decoded.SequenceId = ReadLittleEndianUint32(Cursor + 12);
    return Decoded;
}

