/**
 * @file NetPacketCodec.h
 * @brief 16 字节紧凑小端序网络报文头与完整帧无状态二进制编解码器
 */

#pragma once

#include "CoreMinimal.h"
#include "Codec/NetCodecTypes.h"
#include "Packet/NetPacketTypes.h"

/**
 * @brief 16 字节小端序网络报文头校验、就地零分配编码与高速反解无状态编解码器
 */
class GAMENETCORE_API FNetPacketCodec final {
public:
    FNetPacketCodec() = delete;

public:
    /**
     * @brief 校验报文头魔数、头部尺寸与负载长度上限，返回精确解析状态码
     * @param Header 待校验的报文头
     * @param MaxAllowedPayloadBytes 单包允许的最大负载字节数上限
     * @return 校验通过返回 CompleteFrame，否则返回对应违例枚举
     */
    [[nodiscard]] static ENetFrameParseStatus ValidateHeader(
        const FNetPacketHeader& Header,
        uint32 MaxAllowedPayloadBytes = FNetPacketHeader::DEFAULT_MAX_PAYLOAD_BYTES
    ) noexcept;

    /**
     * @brief 判定报文头是否通过魔数、头部尺寸与负载长度校验
     * @param Header 待校验的报文头
     * @param MaxAllowedPayloadBytes 单包允许的最大负载字节数上限
     * @return 合法返回 true，否则返回 false
     */
    [[nodiscard]] static bool IsValidHeader(
        const FNetPacketHeader& Header,
        uint32 MaxAllowedPayloadBytes = FNetPacketHeader::DEFAULT_MAX_PAYLOAD_BYTES
    ) noexcept;

    /**
     * @brief 将报文头按小端序就地编码写入目标字节缓冲区（调用方需保证至少具备 16 字节可写空间，零堆分配）
     * @param Header 待编码的报文头
     * @param OutWireBytes 目标写入字节缓冲区首地址
     */
    static void EncodeHeaderTo(const FNetPacketHeader& Header, uint8* OutWireBytes) noexcept;

    /**
     * @brief 将单帧报文头与负载直接编码并写入调用方提供的复用缓冲区（自动对齐 PayloadLength，复用容量零重复分配）
     * @param Header 待编码的报文头模板
     * @param Payload 负载二进制切片
     * @param OutFrameBytes 目标写入字节数组（自动就地调整大小，复用底层已预留容量）
     */
    static void EncodeFrameTo(
        const FNetPacketHeader& Header,
        TConstArrayView<uint8> Payload,
        TArray<uint8>& OutFrameBytes
    );

    /**
     * @brief 从二进制切片首部零分配高速反解 16 字节小端序报文头
     * @param WireBytes 原始字节切片
     * @param OutHeader 反解成功时写入的目标报文头引用
     * @return 字节数达到 16 且首地址有效时返回 true，否则返回 false
     */
    [[nodiscard]] static bool TryDecodeHeader(
        TConstArrayView<uint8> WireBytes,
        FNetPacketHeader& OutHeader
    ) noexcept;
};
