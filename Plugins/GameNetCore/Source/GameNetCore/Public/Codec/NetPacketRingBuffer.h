/**
 * @file NetPacketRingBuffer.h
 * @brief 支持粘包切分、半包重组与畸形包/溢出熔断的 2 的幂对齐环形字节流帧解析器
 */

#pragma once

#include "CoreMinimal.h"
#include "Codec/NetCodecTypes.h"
#include "Packet/NetPacketTypes.h"

/**
 * @brief 2 的幂对齐环形字节流缓冲区与零拷贝帧消费器（位掩码寻址、有界背压与重入安全）
 */
class GAMENETCORE_API FNetPacketRingBuffer final {
public:
    static constexpr int32 DEFAULT_INITIAL_CAPACITY_BYTES = 4096;
    static constexpr int32 MIN_CAPACITY_BYTES = 64;
    static constexpr int32 MAX_CAPACITY_BYTES = 1 << 30;

private:
    static constexpr SIZE_T SCRATCH_SHRINK_THRESHOLD_BYTES = 64 * 1024;

public:
    /**
     * @brief 构造指定初始容量与单包上限的环形帧解析器（底层容量自动向上对齐至 2 的幂）
     * @param InitialCapacityBytes 环形缓冲区初始字节容量
     * @param InMaxPayloadBytes 允许接收的单帧最大负载字节数
     */
    explicit FNetPacketRingBuffer(
        int32 InitialCapacityBytes = DEFAULT_INITIAL_CAPACITY_BYTES,
        uint32 InMaxPayloadBytes = FNetPacketHeader::DEFAULT_MAX_PAYLOAD_BYTES
    );

    /**
     * @brief 向环形缓冲区尾部追加新到达的原始网络字节流；若总缓冲超过安全上限则熔断清空并返回 false
     * @param IncomingBytes 新到达的字节切片
     * @return 追加成功返回 true；超过最大安全缓冲上限触发熔断返回 false
     */
    bool Append(TConstArrayView<uint8> IncomingBytes);

    /**
     * @brief 零分配窥探缓冲区头部下一帧报文头与解析状态（不移动读游标）
     * @param OutHeader 成功探测到头部时写入的目标报文头引用
     * @return 解析状态枚举（CompleteFrame 表示整帧已到齐，NeedMoreData 表示半包，其余为畸形包）
     */
    [[nodiscard]] ENetFrameParseStatus PeekNextFrame(FNetPacketHeader& OutHeader) const noexcept;

    /**
     * @brief 单次解头探测并就地以只读切片回调消费一帧数据包（零拷贝快路径 + 重入安全 + 畸形熔断）
     * @param Consumer 消费回调：接收报文头与负载切片。数据物理连续时零拷贝直通，跨环回边界时复用内部重组缓冲
     * @return 消费成功返回 CompleteFrame，半包返回 NeedMoreData，遇到畸形帧自动清空熔断并返回精确违例枚举
     */
    [[nodiscard]] ENetFrameParseStatus TryConsumePacket(
        TFunctionRef<void(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload)> Consumer
    );

    /**
     * @brief 循环消费当前缓冲区内所有已就绪的完整数据包，直至半包或遭遇畸形帧熔断
     * @param Consumer 消费回调：接收每帧报文头与负载切片
     * @param OutConsumedCount 可选输出本次累计成功消费的完整数据包总数
     * @return 终止解析时的精确状态（正常排空返回 NeedMoreData，熔断返回对应违例枚举）
     */
    [[nodiscard]] ENetFrameParseStatus ConsumeAllAvailable(
        TFunctionRef<void(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload)> Consumer,
        int32* OutConsumedCount = nullptr
    );

    /**
     * @brief 重置环形缓冲区的读写游标并丢弃所有未读字节，必要时回收过大的跨界重组缓冲
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
     * @brief 查询当前环形底层连续内存总容量（恒为 2 的幂）
     * @return 容量字节数
     */
    [[nodiscard]] int32 GetCapacityBytes() const noexcept {
        return Buffer.Num();
    }

    /**
     * @brief 查询缓冲区允许自动扩容的最大总字节上限（防恶意半包流 OOM）
     * @return 最大总容量字节数
     */
    [[nodiscard]] int32 GetMaxBufferCapacityBytes() const noexcept {
        return MaxBufferCapacityBytes;
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
     * @brief 将目标字节数向上钳制并规整为安全范围内的 2 的幂容量
     * @param RequestedBytes 请求字节数
     * @return 落在 [MIN_CAPACITY_BYTES, MAX_CAPACITY_BYTES] 内的 2 的幂容量
     */
    [[nodiscard]] static int32 NormalizePowerOfTwoCapacity(int64 RequestedBytes) noexcept;

    /**
     * @brief 确保底层容量足以容纳目标字节数，不足时按 2 的幂策略线性化扩容
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
    TArray<uint8> DefragScratchBuffer;
    int32 HeadIndex{0};
    int32 ReadableBytes{0};
    int32 CapacityMask{0};
    int32 MaxBufferCapacityBytes{DEFAULT_INITIAL_CAPACITY_BYTES};
    uint32 MaxAllowedPayloadBytes{FNetPacketHeader::DEFAULT_MAX_PAYLOAD_BYTES};
    uint64 ResetEpoch{0};
};
