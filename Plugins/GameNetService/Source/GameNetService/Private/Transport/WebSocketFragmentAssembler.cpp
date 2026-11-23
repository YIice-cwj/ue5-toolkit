// Copyright Epic Games, Inc. All Rights Reserved.

#include "Transport/WebSocketFragmentAssembler.h"

/**
 * @brief 构造分片重组器
 * @param InMaxPayloadBytes 单条消息允许携带的最大业务负载字节数
 */
FWebSocketFragmentAssembler::FWebSocketFragmentAssembler(uint32 InMaxPayloadBytes) noexcept
    : MaxFrameBytes(
        static_cast<int64>(FNetPacketHeader::WIRE_HEADER_BYTES) + static_cast<int64>(InMaxPayloadBytes)
    ) {
}

/**
 * @brief 追加一帧 WebSocket 二进制分片并在末分片到达时输出只读视图
 * @param FragmentBytes 当前到达的分片字节切片
 * @param bIsLastFragment 是否为当前 WebSocket 消息的最后一个分片
 * @param OutMessageView 接收完整消息的只读字节视图
 * @return 分片拼装状态枚举
 */
EWebSocketFragmentStatus FWebSocketFragmentAssembler::AppendFragment(
    TConstArrayView<uint8> FragmentBytes,
    bool bIsLastFragment,
    TConstArrayView<uint8>& OutMessageView
) {
    OutMessageView = {};
    if (bHasCompletedMessage) {
        ShrinkOrResetBuffer();
        bHasCompletedMessage = false;
    }

    const int64 ProjectedBytes = static_cast<int64>(FragmentBuffer.Num()) + static_cast<int64>(FragmentBytes.Num());
    if (ProjectedBytes > MaxFrameBytes) {
        Reset();
        return EWebSocketFragmentStatus::FrameExceedsLimit;
    }

    if (FragmentBuffer.IsEmpty() && bIsLastFragment) {
        OutMessageView = FragmentBytes;
        return EWebSocketFragmentStatus::MessageReady;
    }

    if (!FragmentBytes.IsEmpty()) {
        const int32 OldNum = FragmentBuffer.Num();
        FragmentBuffer.SetNumUninitialized(OldNum + FragmentBytes.Num());
        FMemory::Memcpy(FragmentBuffer.GetData() + OldNum, FragmentBytes.GetData(), FragmentBytes.Num());
    }

    if (!bIsLastFragment) {
        return EWebSocketFragmentStatus::Incomplete;
    }

    bHasCompletedMessage = true;
    OutMessageView = FragmentBuffer;
    return EWebSocketFragmentStatus::MessageReady;
}

/**
 * @brief 清空所有未收齐的半消息分片缓存，必要时回收过大的堆内存
 */
void FWebSocketFragmentAssembler::Reset() noexcept {
    ShrinkOrResetBuffer();
    bHasCompletedMessage = false;
}

/**
 * @brief 清理内部累积缓存，超出回收阈值时释放堆内存
 */
void FWebSocketFragmentAssembler::ShrinkOrResetBuffer() noexcept {
    if (FragmentBuffer.GetAllocatedSize() > BUFFER_SHRINK_THRESHOLD_BYTES) {
        FragmentBuffer.Empty();
    } else {
        FragmentBuffer.Reset();
    }
}
