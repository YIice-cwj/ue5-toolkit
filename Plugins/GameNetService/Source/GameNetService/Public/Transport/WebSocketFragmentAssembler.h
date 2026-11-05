/**
 * @file WebSocketFragmentAssembler.h
 * @brief 带最大帧长防溢出保护的 WebSocket 二进制分片消息重组器
 */

#pragma once

#include "CoreMinimal.h"
#include "Packet/NetPacketTypes.h"
#include "Transport/WebSocketTransportTypes.h"

/**
 * @brief WebSocket 多分片二进制消息累加与超限拦截组件
 */
class GAMENETSERVICE_API FWebSocketFragmentAssembler final {
public:
    /**
     * @brief 构造分片重组器
     * @param InMaxPayloadBytes 单条消息允许携带的最大业务负载字节数
     */
    explicit FWebSocketFragmentAssembler(
        uint32 InMaxPayloadBytes = FNetPacketHeader::DEFAULT_MAX_PAYLOAD_BYTES
    ) noexcept;

    /**
     * @brief 追加一帧 WebSocket 二进制分片并在末分片到达时产出完整消息；超出上限时清空并返回超限状态
     * @param FragmentBytes 当前到达的分片字节切片
     * @param bIsLastFragment 是否为当前 WebSocket 消息的最后一个分片
     * @return 包含组装状态与完整字节消息的结果值对象
     */
    [[nodiscard]] FWebSocketFragmentOutcome AppendFragment(
        TConstArrayView<uint8> FragmentBytes,
        bool bIsLastFragment
    );

    /**
     * @brief 清空所有未收齐的半消息分片缓存
     */
    void Reset() noexcept;

    /**
     * @brief 查询当前已累积但尚未收齐末分片的字节总数
     * @return 当前分片缓存字节数
     */
    [[nodiscard]] int32 GetBufferedFragmentBytes() const noexcept {
        return FragmentBuffer.Num();
    }

    /**
     * @brief 查询单条消息允许的最大帧总字节数（含 16 字节线格式报文头）
     * @return 最大帧总字节数
     */
    [[nodiscard]] int64 GetMaxFrameBytes() const noexcept {
        return MaxFrameBytes;
    }

private:
    TArray<uint8> FragmentBuffer;
    int64 MaxFrameBytes{FNetPacketHeader::WIRE_HEADER_BYTES + FNetPacketHeader::DEFAULT_MAX_PAYLOAD_BYTES};
};

