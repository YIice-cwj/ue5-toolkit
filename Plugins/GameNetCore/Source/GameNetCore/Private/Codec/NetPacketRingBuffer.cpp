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
    FNetPacketHeader Header;
    const ENetFrameParseStatus Status = PeekNextFrame(Header);
    if (Status == ENetFrameParseStatus::NeedMoreData) {
        return FNetFrameDecodeOutcome{ENetFrameParseStatus::NeedMoreData, {}};
    }
    if (Status != ENetFrameParseStatus::CompleteFrame) {
        Reset();
        return FNetFrameDecodeOutcome{Status, {}};
    }

    const int32 PayloadBytes = static_cast<int32>(Header.PayloadLength);
    const int32 TotalFrameBytes = FNetPacketHeader::WIRE_HEADER_BYTES + PayloadBytes;

    FNetFrameDecodeOutcome Outcome;
    Outcome.Status = ENetFrameParseStatus::CompleteFrame;
    Outcome.Packet.Header = Header;
    if (PayloadBytes > 0) {
        Outcome.Packet.Payload.SetNumUninitialized(PayloadBytes);
        CopyFromOffset(FNetPacketHeader::WIRE_HEADER_BYTES, Outcome.Packet.Payload.GetData(), PayloadBytes);
    }

    ConsumeBytes(TotalFrameBytes);
    return Outcome;
}

/**
 * @brief 零分配窥探缓冲区头部下一帧报文头与解析状态（不移动读游标）
 * @param OutHeader 成功探测到合法头部时写入的目标报文头引用
 * @return 解析状态枚举
 */
ENetFrameParseStatus FNetPacketRingBuffer::PeekNextFrame(FNetPacketHeader& OutHeader) const noexcept {
    if (ReadableBytes < FNetPacketHeader::WIRE_HEADER_BYTES) {
        return ENetFrameParseStatus::NeedMoreData;
    }

    const int32 Capacity = Buffer.Num();
    bool bDecoded = false;
    if (HeadIndex + FNetPacketHeader::WIRE_HEADER_BYTES <= Capacity) {
        bDecoded = FNetPacketCodec::TryDecodeHeaderFast(
            Buffer.GetData() + HeadIndex,
            FNetPacketHeader::WIRE_HEADER_BYTES,
            OutHeader
        );
    } else {
        uint8 RawHeader[FNetPacketHeader::WIRE_HEADER_BYTES];
        CopyFromOffset(0, RawHeader, FNetPacketHeader::WIRE_HEADER_BYTES);
        bDecoded = FNetPacketCodec::TryDecodeHeaderFast(
            RawHeader,
            FNetPacketHeader::WIRE_HEADER_BYTES,
            OutHeader
        );
    }

    if (!bDecoded) {
        return ENetFrameParseStatus::InvalidMagic;
    }

    const ENetFrameParseStatus ValidationStatus = FNetPacketCodec::ValidateHeader(OutHeader, MaxAllowedPayloadBytes);
    if (ValidationStatus != ENetFrameParseStatus::CompleteFrame) {
        return ValidationStatus;
    }

    const int64 TotalFrameBytes =
        static_cast<int64>(FNetPacketHeader::WIRE_HEADER_BYTES) + static_cast<int64>(OutHeader.PayloadLength);
    if (static_cast<int64>(ReadableBytes) < TotalFrameBytes) {
        return ENetFrameParseStatus::NeedMoreData;
    }

    return ENetFrameParseStatus::CompleteFrame;
}

/**
 * @brief 尝试就地探测并直接以只读切片回调消费一帧数据包（无堆内存分配零拷贝路径）
 * @param Consumer 消费回调
 * @return 成功消费一帧返回 true；半包或畸形熔断清空返回 false
 */
bool FNetPacketRingBuffer::TryConsumePacket(
    TFunctionRef<void(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload)> Consumer
) {
    FNetPacketHeader Header;
    const ENetFrameParseStatus Status = PeekNextFrame(Header);
    if (Status == ENetFrameParseStatus::NeedMoreData) {
        return false;
    }
    if (Status != ENetFrameParseStatus::CompleteFrame) {
        Reset();
        return false;
    }

    const int32 PayloadBytes = static_cast<int32>(Header.PayloadLength);
    const int32 TotalFrameBytes = FNetPacketHeader::WIRE_HEADER_BYTES + PayloadBytes;

    if (PayloadBytes == 0) {
        Consumer(Header, {});
    } else {
        const int32 Capacity = Buffer.Num();
        const int32 PayloadStart = (HeadIndex + FNetPacketHeader::WIRE_HEADER_BYTES) % Capacity;
        if (PayloadStart + PayloadBytes <= Capacity) {
            Consumer(Header, TConstArrayView<uint8>(Buffer.GetData() + PayloadStart, PayloadBytes));
        } else {
            DefragScratchBuffer.SetNumUninitialized(PayloadBytes);
            CopyFromOffset(FNetPacketHeader::WIRE_HEADER_BYTES, DefragScratchBuffer.GetData(), PayloadBytes);
            Consumer(Header, DefragScratchBuffer);
        }
    }

    ConsumeBytes(TotalFrameBytes);
    return true;
}

/**
 * @brief 循环消费当前缓冲区内所有已就绪的完整数据包，直至半包或遭遇熔断
 * @param Consumer 消费回调：接收每帧报文头与负载切片
 * @return 累计成功消费的数据包总数
 */
int32 FNetPacketRingBuffer::ConsumeAllAvailable(
    TFunctionRef<void(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload)> Consumer
) {
    int32 ConsumedTotal = 0;
    while (TryConsumePacket(Consumer)) {
        ++ConsumedTotal;
    }
    return ConsumedTotal;
}

/**
 * @brief 重置环形缓冲区的读写游标并丢弃所有未读字节
 */
void FNetPacketRingBuffer::Reset() noexcept {
    HeadIndex = 0;
    ReadableBytes = 0;
    if (DefragScratchBuffer.GetAllocatedSize() > 64 * 1024) {
        DefragScratchBuffer.Empty();
    } else {
        DefragScratchBuffer.Reset();
    }
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
