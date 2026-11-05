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
 * @brief 追加一帧 WebSocket 二进制分片并在末分片到达时产出完整消息；超出上限时清空并返回超限状态
 * @param FragmentBytes 当前到达的分片字节切片
 * @param bIsLastFragment 是否为当前 WebSocket 消息的最后一个分片
 * @return 包含组装状态与完整字节消息的结果值对象
 */
FWebSocketFragmentOutcome FWebSocketFragmentAssembler::AppendFragment(
    TConstArrayView<uint8> FragmentBytes,
    bool bIsLastFragment
) {
    const int64 ProjectedBytes = static_cast<int64>(FragmentBuffer.Num()) + static_cast<int64>(FragmentBytes.Num());
    if (ProjectedBytes > MaxFrameBytes) {
        Reset();
        return FWebSocketFragmentOutcome{EWebSocketFragmentStatus::FrameExceedsLimit, {}};
    }

    if (FragmentBuffer.IsEmpty() && bIsLastFragment) {
        FWebSocketFragmentOutcome ReadyOutcome;
        ReadyOutcome.Status = EWebSocketFragmentStatus::MessageReady;
        if (!FragmentBytes.IsEmpty()) {
            ReadyOutcome.AssembledMessage.Append(FragmentBytes.GetData(), FragmentBytes.Num());
        }
        return ReadyOutcome;
    }

    if (!FragmentBytes.IsEmpty()) {
        FragmentBuffer.Append(FragmentBytes.GetData(), FragmentBytes.Num());
    }
    if (!bIsLastFragment) {
        return FWebSocketFragmentOutcome{EWebSocketFragmentStatus::Incomplete, {}};
    }

    FWebSocketFragmentOutcome ReadyOutcome;
    ReadyOutcome.Status = EWebSocketFragmentStatus::MessageReady;
    ReadyOutcome.AssembledMessage = MoveTemp(FragmentBuffer);
    FragmentBuffer.Reset();
    return ReadyOutcome;
}

/**
 * @brief 清空所有未收齐的半消息分片缓存
 */
void FWebSocketFragmentAssembler::Reset() noexcept {
    FragmentBuffer.Reset();
}

