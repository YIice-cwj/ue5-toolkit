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
 * @brief 追加一帧 WebSocket 二进制分片并在末分片到达时就地写入外部复用数组（零额外堆分配）
 * @param FragmentBytes 当前到达的分片字节切片
 * @param bIsLastFragment 是否为当前 WebSocket 消息的最后一个分片
 * @param OutMessage 接收完整拼装消息的外部复用数组
 * @return 分片拼装状态枚举
 */
EWebSocketFragmentStatus FWebSocketFragmentAssembler::AppendFragment(
    TConstArrayView<uint8> FragmentBytes,
    bool bIsLastFragment,
    TArray<uint8>& OutMessage
) {
    OutMessage.Reset();

    const int64 ProjectedBytes = static_cast<int64>(FragmentBuffer.Num()) + static_cast<int64>(FragmentBytes.Num());
    if (ProjectedBytes > MaxFrameBytes) {
        Reset();
        return EWebSocketFragmentStatus::FrameExceedsLimit;
    }

    if (FragmentBuffer.IsEmpty() && bIsLastFragment) {
        if (!FragmentBytes.IsEmpty()) {
            OutMessage.SetNumUninitialized(FragmentBytes.Num());
            FMemory::Memcpy(OutMessage.GetData(), FragmentBytes.GetData(), FragmentBytes.Num());
        }
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

    OutMessage.SetNumUninitialized(FragmentBuffer.Num());
    FMemory::Memcpy(OutMessage.GetData(), FragmentBuffer.GetData(), FragmentBuffer.Num());
    FragmentBuffer.Reset();
    return EWebSocketFragmentStatus::MessageReady;
}

/**
 * @brief 清空所有未收齐的半消息分片缓存
 */
void FWebSocketFragmentAssembler::Reset() noexcept {
    FragmentBuffer.Reset();
}

