// Copyright Epic Games, Inc. All Rights Reserved.

#include "Codec/NetPacketRingBuffer.h"

#include "Codec/NetPacketCodec.h"
#include "HAL/UnrealMemory.h"

/**
 * @brief 构造指定初始容量与单包上限的环形帧解析器
 * @param InitialCapacityBytes 环形缓冲区初始字节容量
 * @param InMaxPayloadBytes 允许接收的单帧最大负载字节数
 */
FNetPacketRingBuffer::FNetPacketRingBuffer(int32 InitialCapacityBytes, uint32 InMaxPayloadBytes)
    : MaxAllowedPayloadBytes(InMaxPayloadBytes) {
    const int32 NormalizedCapacity = FMath::Max(InitialCapacityBytes, MIN_CAPACITY_BYTES);
    Buffer.SetNumUninitialized(NormalizedCapacity);
}

/**
 * @brief 向环形缓冲区尾部追加新到达的原始网络字节流
 * @param IncomingBytes 新到达的字节切片
 */
void FNetPacketRingBuffer::Append(TConstArrayView<uint8> IncomingBytes) {
    if (IncomingBytes.IsEmpty()) {
        return;
    }

    EnsureCapacity(ReadableBytes + IncomingBytes.Num());

    const int32 Capacity = Buffer.Num();
    const int32 TailIndex = (HeadIndex + ReadableBytes) % Capacity;
    const int32 FirstChunkBytes = FMath::Min(IncomingBytes.Num(), Capacity - TailIndex);
    FMemory::Memcpy(Buffer.GetData() + TailIndex, IncomingBytes.GetData(), FirstChunkBytes);

    const int32 SecondChunkBytes = IncomingBytes.Num() - FirstChunkBytes;
    if (SecondChunkBytes > 0) {
        FMemory::Memcpy(Buffer.GetData(), IncomingBytes.GetData() + FirstChunkBytes, SecondChunkBytes);
    }

    ReadableBytes += IncomingBytes.Num();
}

/**
 * @brief 尝试从缓冲区头部解析并弹出一帧完整数据包；遇到非法魔数或超限负载立即清空熔断
 * @return 包含解析状态枚举与提取数据包的帧解析结果
 */
FNetFrameDecodeOutcome FNetPacketRingBuffer::TryPopPacket() {
    if (ReadableBytes < FNetPacketHeader::WIRE_HEADER_BYTES) {
        return FNetFrameDecodeOutcome{ENetFrameParseStatus::NeedMoreData, {}};
    }

    uint8 RawHeader[FNetPacketHeader::WIRE_HEADER_BYTES];
    CopyFromOffset(0, RawHeader, FNetPacketHeader::WIRE_HEADER_BYTES);

    const TGameStatusResult<FNetPacketHeader> ParsedHeader = FNetPacketCodec::TryDecodeHeader(RawHeader);
    if (ParsedHeader.IsFailure()) {
        Reset();
        return FNetFrameDecodeOutcome{ENetFrameParseStatus::InvalidMagic, {}};
    }
    const ENetFrameParseStatus ValidationStatus = FNetPacketCodec::ValidateHeader(ParsedHeader.GetValue(), MaxAllowedPayloadBytes);
    if (ValidationStatus != ENetFrameParseStatus::CompleteFrame) {
        Reset();
        return FNetFrameDecodeOutcome{ValidationStatus, {}};
    }

    const int64 TotalFrameBytes =
        static_cast<int64>(FNetPacketHeader::WIRE_HEADER_BYTES) + static_cast<int64>(ParsedHeader.GetValue().PayloadLength);
    if (static_cast<int64>(ReadableBytes) < TotalFrameBytes) {
        return FNetFrameDecodeOutcome{ENetFrameParseStatus::NeedMoreData, {}};
    }

    FNetFrameDecodeOutcome Outcome;
    Outcome.Status = ENetFrameParseStatus::CompleteFrame;
    Outcome.Packet.Header = ParsedHeader.GetValue();
    if (ParsedHeader.GetValue().PayloadLength > 0) {
        const int32 PayloadBytes = static_cast<int32>(ParsedHeader.GetValue().PayloadLength);
        Outcome.Packet.Payload.SetNumUninitialized(PayloadBytes);
        CopyFromOffset(FNetPacketHeader::WIRE_HEADER_BYTES, Outcome.Packet.Payload.GetData(), PayloadBytes);
    }

    ConsumeBytes(static_cast<int32>(TotalFrameBytes));
    return Outcome;
}

/**
 * @brief 重置环形缓冲区的读写游标并丢弃所有未读字节
 */
void FNetPacketRingBuffer::Reset() noexcept {
    HeadIndex = 0;
    ReadableBytes = 0;
}

/**
 * @brief 确保底层容量足以容纳目标字节数，不足时按倍增策略线性化扩容
 * @param RequiredTotalBytes 所需最小总容量字节数
 */
void FNetPacketRingBuffer::EnsureCapacity(int32 RequiredTotalBytes) {
    if (RequiredTotalBytes <= Buffer.Num()) {
        return;
    }

    int32 NewCapacity = FMath::Max(Buffer.Num(), MIN_CAPACITY_BYTES);
    while (NewCapacity < RequiredTotalBytes) {
        NewCapacity *= 2;
    }

    TArray<uint8> ExpandedBuffer;
    ExpandedBuffer.SetNumUninitialized(NewCapacity);
    if (ReadableBytes > 0) {
        CopyFromOffset(0, ExpandedBuffer.GetData(), ReadableBytes);
    }

    Buffer = MoveTemp(ExpandedBuffer);
    HeadIndex = 0;
}

/**
 * @brief 从距离读游标指定偏移处复制连续字节至目标内存（不移动读游标，自动跨越环回边界）
 * @param OffsetBytes 相对于读游标的起始字节偏移
 * @param Destination 目标写入字节缓冲区首地址
 * @param ByteCount 待复制字节数
 */
void FNetPacketRingBuffer::CopyFromOffset(int32 OffsetBytes, uint8* Destination, int32 ByteCount) const noexcept {
    if (ByteCount <= 0) {
        return;
    }

    const int32 Capacity = Buffer.Num();
    const int32 StartIndex = (HeadIndex + OffsetBytes) % Capacity;
    const int32 FirstChunkBytes = FMath::Min(ByteCount, Capacity - StartIndex);
    FMemory::Memcpy(Destination, Buffer.GetData() + StartIndex, FirstChunkBytes);

    const int32 SecondChunkBytes = ByteCount - FirstChunkBytes;
    if (SecondChunkBytes > 0) {
        FMemory::Memcpy(Destination + FirstChunkBytes, Buffer.GetData(), SecondChunkBytes);
    }
}

/**
 * @brief 推进读游标以消费指定数量的字节
 * @param ByteCount 消费字节数
 */
void FNetPacketRingBuffer::ConsumeBytes(int32 ByteCount) noexcept {
    HeadIndex = (HeadIndex + ByteCount) % Buffer.Num();
    ReadableBytes -= ByteCount;
    if (ReadableBytes == 0) {
        HeadIndex = 0;
    }
}
