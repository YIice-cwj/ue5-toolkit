/**
 * @file NetPacketRingBuffer.h
 * @brief 支持粘包切分、半包重组与畸形包熔断的环形字节流帧解析器
 */

#pragma once

#include "CoreMinimal.h"
#include "Codec/NetCodecTypes.h"
#include "Packet/NetPacketTypes.h"

/**
 * @brief 环形字节流缓冲区与帧提取器（单接收管线内零冗余搬移解包）
 */
class GAMENETCORE_API FNetPacketRingBuffer final {
public:
    static constexpr int32 DEFAULT_INITIAL_CAPACITY_BYTES = 4096;
    static constexpr int32 MIN_CAPACITY_BYTES = 64;

public:
    /**
     * @brief 构造指定初始容量与单包上限的环形帧解析器
     * @param InitialCapacityBytes 环形缓冲区初始字节容量
     * @param InMaxPayloadBytes 允许接收的单帧最大负载字节数
     */
    explicit FNetPacketRingBuffer(
        int32 InitialCapacityBytes = DEFAULT_INITIAL_CAPACITY_BYTES,
        uint32 InMaxPayloadBytes = FNetPacketHeader::DEFAULT_MAX_PAYLOAD_BYTES
    );

    /**
     * @brief 向环形缓冲区尾部追加新到达的原始网络字节流
     * @param IncomingBytes 新到达的字节切片
     */
    void Append(TConstArrayView<uint8> IncomingBytes);

    /**
     * @brief 尝试从缓冲区头部解析并弹出一帧完整数据包；遇到非法魔数或超限负载立即清空熔断
     * @return 包含解析状态枚举与提取数据包的帧解析结果
     */
    [[nodiscard]] FNetFrameDecodeOutcome TryPopPacket();

    /**
     * @brief 重置环形缓冲区的读写游标并丢弃所有未读字节
     */
    void Reset() noexcept;

    /**
     * @brief 查询当前缓冲区内待消费的字节总数
     * @return 未读字节数
     */
    [[nodiscard]] int32 GetReadableBytes() const noexcept {
        return ReadableBytes;
    }

    /**
     * @brief 查询当前环形底层连续内存总容量
     * @return 容量字节数
     */
    [[nodiscard]] int32 GetCapacityBytes() const noexcept {
        return Buffer.Num();
    }

    /**
     * @brief 查询单包允许的最大负载字节阈值
     * @return 最大负载字节数
     */
    [[nodiscard]] uint32 GetMaxPayloadBytes() const noexcept {
        return MaxAllowedPayloadBytes;
    }

private:
    /**
     * @brief 确保底层容量足以容纳目标字节数，不足时按倍增策略线性化扩容
     * @param RequiredTotalBytes 所需最小总容量字节数
     */
    void EnsureCapacity(int32 RequiredTotalBytes);

    /**
     * @brief 从距离读游标指定偏移处复制连续字节至目标内存（不移动读游标，自动跨越环回边界）
     * @param OffsetBytes 相对于读游标的起始字节偏移
     * @param Destination 目标写入字节缓冲区首地址
     * @param ByteCount 待复制字节数
     */
    void CopyFromOffset(int32 OffsetBytes, uint8* Destination, int32 ByteCount) const noexcept;

    /**
     * @brief 推进读游标以消费指定数量的字节
     * @param ByteCount 消费字节数
     */
    void ConsumeBytes(int32 ByteCount) noexcept;

private:
    TArray<uint8> Buffer;
    int32 HeadIndex{0};
    int32 ReadableBytes{0};
    uint32 MaxAllowedPayloadBytes{FNetPacketHeader::DEFAULT_MAX_PAYLOAD_BYTES};
};
