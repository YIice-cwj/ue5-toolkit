/**
 * @file WebSocketFragmentAssembler.h
 * @brief 带最大帧长防溢出保护的 WebSocket 二进制分片零拷贝视图重组器
 */

#pragma once

#include "CoreMinimal.h"
#include "Packet/NetPacketTypes.h"
#include "Transport/WebSocketTransportTypes.h"

/**
 * @brief WebSocket 多分片二进制消息累加与超限拦截组件（单分片零拷贝直通，多分片单次拷贝交付视图）
 */
class GAMENETSERVICE_API FWebSocketFragmentAssembler final {
private:
    static constexpr SIZE_T BUFFER_SHRINK_THRESHOLD_BYTES = 64 * 1024;

public:
    /**
     * @brief 构造分片重组器
     * @param InMaxPayloadBytes 单条消息允许携带的最大业务负载字节数
     */
    explicit FWebSocketFragmentAssembler(
        uint32 InMaxPayloadBytes = FNetPacketHeader::DEFAULT_MAX_PAYLOAD_BYTES
    ) noexcept;

    /**
     * @brief 追加一帧 WebSocket 二进制分片并在末分片到达时输出只读视图（单分片零拷贝直指原切片，多分片指向内部缓存）
     * @param FragmentBytes 当前到达的分片字节切片
     * @param bIsLastFragment 是否为当前 WebSocket 消息的最后一个分片
     * @param OutMessageView 接收完整消息的只读字节视图（有效期维持至下一次 AppendFragment 或 Reset 调用）
     * @return 分片拼装状态枚举
     */
    [[nodiscard]] EWebSocketFragmentStatus AppendFragment(
        TConstArrayView<uint8> FragmentBytes,
        bool bIsLastFragment,
        TConstArrayView<uint8>& OutMessageView
    );

    /**
     * @brief 清空所有未收齐的半消息分片缓存，必要时回收过大的堆内存
     */
    void Reset() noexcept;

    /**
     * @brief 查询当前已累积但尚未收齐末分片的字节总数
     * @return 当前分片缓存字节数
     */
    [[nodiscard]] int32 GetBufferedFragmentBytes() const noexcept {
        return bHasCompletedMessage ? 0 : FragmentBuffer.Num();
    }

    /**
     * @brief 查询单条消息允许的最大帧总字节数（含 16 字节线格式报文头）
     * @return 最大帧总字节数
     */
    [[nodiscard]] int64 GetMaxFrameBytes() const noexcept {
        return MaxFrameBytes;
    }

private:
    /**
     * @brief 清理内部累积缓存，超出回收阈值时释放堆内存
     */
    void ShrinkOrResetBuffer() noexcept;

private:
    TArray<uint8> FragmentBuffer;
    int64 MaxFrameBytes{FNetPacketHeader::WIRE_HEADER_BYTES + FNetPacketHeader::DEFAULT_MAX_PAYLOAD_BYTES};
    bool bHasCompletedMessage{false};
};
