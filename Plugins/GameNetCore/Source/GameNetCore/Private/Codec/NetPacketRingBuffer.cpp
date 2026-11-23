// Copyright Epic Games, Inc. All Rights Reserved.

#include "Codec/NetPacketRingBuffer.h"

#include "Codec/NetPacketCodec.h"
#include "HAL/UnrealMemory.h"
#include "Logging/GameLogCategories.h"

/**
 * @brief 构造指定初始容量与单包上限的环形帧解析器（底层容量自动向上对齐至 2 的幂）
 * @param InitialCapacityBytes 环形缓冲区初始字节容量
 * @param InMaxPayloadBytes 允许接收的单帧最大负载字节数
 */
FNetPacketRingBuffer::FNetPacketRingBuffer(int32 InitialCapacityBytes, uint32 InMaxPayloadBytes)
    : MaxAllowedPayloadBytes(InMaxPayloadBytes) {
    const int32 NormalizedInitial = NormalizePowerOfTwoCapacity(InitialCapacityBytes);
    const int64 MinUpperBound = FMath::Max<int64>(
        NormalizedInitial,
        (static_cast<int64>(FNetPacketHeader::WIRE_HEADER_BYTES) + static_cast<int64>(MaxAllowedPayloadBytes)) * 2
    );
    MaxBufferCapacityBytes = NormalizePowerOfTwoCapacity(MinUpperBound);

    Buffer.SetNumUninitialized(NormalizedInitial);
    CapacityMask = NormalizedInitial - 1;
}

/**
 * @brief 向环形缓冲区尾部追加新到达的原始网络字节流；若总缓冲超过安全上限则熔断清空并返回 false
 * @param IncomingBytes 新到达的字节切片
 * @return 追加成功返回 true；超过最大安全缓冲上限触发熔断返回 false
 */
bool FNetPacketRingBuffer::Append(TConstArrayView<uint8> IncomingBytes) {
    if (IncomingBytes.IsEmpty()) {
        return true;
    }

    const int64 RequiredTotalBytes = static_cast<int64>(ReadableBytes) + static_cast<int64>(IncomingBytes.Num());
    if (RequiredTotalBytes > static_cast<int64>(MaxBufferCapacityBytes)) {
        GAME_LOG_THROTTLE(
            1.0,
            Warning,
            TEXT("[RingBuffer] 追加字节流触发安全容量上限熔断: Readable=%d, Incoming=%d, MaxCapacity=%d"),
            ReadableBytes,
            IncomingBytes.Num(),
            MaxBufferCapacityBytes
        );
        Reset();
        return false;
    }

    EnsureCapacity(static_cast<int32>(RequiredTotalBytes));

    const int32 Capacity = Buffer.Num();
    const int32 TailIndex = (HeadIndex + ReadableBytes) & CapacityMask;
    const int32 FirstChunkBytes = FMath::Min(IncomingBytes.Num(), Capacity - TailIndex);
    FMemory::Memcpy(Buffer.GetData() + TailIndex, IncomingBytes.GetData(), FirstChunkBytes);

    const int32 SecondChunkBytes = IncomingBytes.Num() - FirstChunkBytes;
    if (SecondChunkBytes > 0) {
        FMemory::Memcpy(Buffer.GetData(), IncomingBytes.GetData() + FirstChunkBytes, SecondChunkBytes);
    }

    ReadableBytes += IncomingBytes.Num();
    return true;
}

/**
 * @brief 零分配窥探缓冲区头部下一帧报文头与解析状态（不移动读游标）
 * @param OutHeader 成功探测到头部时写入的目标报文头引用
 * @return 解析状态枚举
 */
ENetFrameParseStatus FNetPacketRingBuffer::PeekNextFrame(FNetPacketHeader& OutHeader) const noexcept {
    if (ReadableBytes < FNetPacketHeader::WIRE_HEADER_BYTES) {
        return ENetFrameParseStatus::NeedMoreData;
    }

    const int32 Capacity = Buffer.Num();
    if (HeadIndex + FNetPacketHeader::WIRE_HEADER_BYTES <= Capacity) {
        (void)FNetPacketCodec::TryDecodeHeader(
            TConstArrayView<uint8>(Buffer.GetData() + HeadIndex, FNetPacketHeader::WIRE_HEADER_BYTES),
            OutHeader
        );
    } else {
        uint8 RawHeader[FNetPacketHeader::WIRE_HEADER_BYTES];
        CopyFromOffset(0, RawHeader, FNetPacketHeader::WIRE_HEADER_BYTES);
        (void)FNetPacketCodec::TryDecodeHeader(RawHeader, OutHeader);
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
 * @brief 单次解头探测并就地以只读切片回调消费一帧数据包（零拷贝快路径 + 重入安全 + 畸形熔断）
 * @param Consumer 消费回调
 * @return 解析与消费状态枚举
 */
ENetFrameParseStatus FNetPacketRingBuffer::TryConsumePacket(
    TFunctionRef<void(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload)> Consumer
) {
    FNetPacketHeader Header;
    const ENetFrameParseStatus Status = PeekNextFrame(Header);
    if (Status == ENetFrameParseStatus::NeedMoreData) {
        return ENetFrameParseStatus::NeedMoreData;
    }
    if (Status != ENetFrameParseStatus::CompleteFrame) {
        GAME_LOG_THROTTLE(
            1.0,
            Warning,
            TEXT("[RingBuffer] 检测到畸形网络帧并触发熔断清空: Status=%d, Magic=0x%04X, HeaderSize=%u, PayloadLen=%u"),
            static_cast<int32>(Status),
            Header.Magic,
            Header.HeaderSize,
            Header.PayloadLength
        );
        Reset();
        return Status;
    }

    const int32 PayloadBytes = static_cast<int32>(Header.PayloadLength);
    const int32 TotalFrameBytes = FNetPacketHeader::WIRE_HEADER_BYTES + PayloadBytes;
    const uint64 EpochBeforeConsumer = ResetEpoch;

    if (PayloadBytes == 0) {
        Consumer(Header, {});
    } else {
        const int32 Capacity = Buffer.Num();
        const int32 PayloadStart = (HeadIndex + FNetPacketHeader::WIRE_HEADER_BYTES) & CapacityMask;
        if (PayloadStart + PayloadBytes <= Capacity) {
            Consumer(Header, TConstArrayView<uint8>(Buffer.GetData() + PayloadStart, PayloadBytes));
        } else {
            DefragScratchBuffer.SetNumUninitialized(PayloadBytes);
            CopyFromOffset(FNetPacketHeader::WIRE_HEADER_BYTES, DefragScratchBuffer.GetData(), PayloadBytes);
            Consumer(Header, DefragScratchBuffer);
        }
    }

    if (ResetEpoch == EpochBeforeConsumer) {
        ConsumeBytes(TotalFrameBytes);
    }
    return ENetFrameParseStatus::CompleteFrame;
}

/**
 * @brief 循环消费当前缓冲区内所有已就绪的完整数据包，直至半包或遭遇畸形帧熔断
 * @param Consumer 消费回调：接收每帧报文头与负载切片
 * @param OutConsumedCount 可选输出累计消费数据包总数
 * @return 终止解析时的精确状态枚举
 */
ENetFrameParseStatus FNetPacketRingBuffer::ConsumeAllAvailable(
    TFunctionRef<void(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload)> Consumer,
    int32* OutConsumedCount
) {
    int32 ConsumedTotal = 0;
    ENetFrameParseStatus TerminalStatus = ENetFrameParseStatus::NeedMoreData;
    for (;;) {
        TerminalStatus = TryConsumePacket(Consumer);
        if (TerminalStatus != ENetFrameParseStatus::CompleteFrame) {
            break;
        }
        ++ConsumedTotal;
    }

    if (OutConsumedCount != nullptr) {
        *OutConsumedCount = ConsumedTotal;
    }
    return TerminalStatus;
}

/**
 * @brief 重置环形缓冲区的读写游标并丢弃所有未读字节，必要时回收过大的跨界重组缓冲
 */
void FNetPacketRingBuffer::Reset() noexcept {
    ++ResetEpoch;
    HeadIndex = 0;
    ReadableBytes = 0;
    if (DefragScratchBuffer.GetAllocatedSize() > SCRATCH_SHRINK_THRESHOLD_BYTES) {
        DefragScratchBuffer.Empty();
    } else {
        DefragScratchBuffer.Reset();
    }
}

/**
 * @brief 将目标字节数向上钳制并规整为安全范围内的 2 的幂容量
 * @param RequestedBytes 请求字节数
 * @return 落在 [MIN_CAPACITY_BYTES, MAX_CAPACITY_BYTES] 内的 2 的幂容量
 */
int32 FNetPacketRingBuffer::NormalizePowerOfTwoCapacity(int64 RequestedBytes) noexcept {
    if (RequestedBytes <= MIN_CAPACITY_BYTES) {
        return MIN_CAPACITY_BYTES;
    }
    if (RequestedBytes >= MAX_CAPACITY_BYTES) {
        return MAX_CAPACITY_BYTES;
    }
    return static_cast<int32>(FMath::RoundUpToPowerOfTwo(static_cast<uint32>(RequestedBytes)));
}

/**
 * @brief 确保底层容量足以容纳目标字节数，不足时按 2 的幂策略线性化扩容
 * @param RequiredTotalBytes 所需最小总容量字节数
 */
void FNetPacketRingBuffer::EnsureCapacity(int32 RequiredTotalBytes) {
    if (RequiredTotalBytes <= Buffer.Num()) {
        return;
    }

    const int32 NewCapacity = NormalizePowerOfTwoCapacity(RequiredTotalBytes);
    TArray<uint8> ExpandedBuffer;
    ExpandedBuffer.SetNumUninitialized(NewCapacity);
    if (ReadableBytes > 0) {
        CopyFromOffset(0, ExpandedBuffer.GetData(), ReadableBytes);
    }

    Buffer = MoveTemp(ExpandedBuffer);
    CapacityMask = NewCapacity - 1;
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
    const int32 StartIndex = (HeadIndex + OffsetBytes) & CapacityMask;
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
    HeadIndex = (HeadIndex + ByteCount) & CapacityMask;
    ReadableBytes -= ByteCount;
    if (ReadableBytes == 0) {
        HeadIndex = 0;
    }
}
