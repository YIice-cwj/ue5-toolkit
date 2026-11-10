/**
 * @file NetPacketCodec.h
 * @brief 16 字节紧凑小端序网络报文头与完整帧无状态二进制编解码器
 */

#pragma once

#include "CoreMinimal.h"
#include "Codec/NetCodecTypes.h"
#include "Packet/NetPacketTypes.h"
#include "Types/TGameResult.h"

/**
 * @brief 16 字节小端序网络报文头校验、编码与完整帧拼装无状态编解码器
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
     * @brief 将报文头编码为跨平台一致的 16 字节小端序二进制数组
     * @param Header 待编码的报文头
     * @return 长度为 16 字节的线格式数组
     */
    [[nodiscard]] static TArray<uint8> EncodeHeader(const FNetPacketHeader& Header);

    /**
     * @brief 将报文头与负载拼装为单帧完整线格式字节数组（自动对齐 PayloadLength）
     * @param Header 待编码的报文头模板
     * @param Payload 负载二进制切片
     * @return 包含 16 字节头与完整负载的连续字节数组
     */
    [[nodiscard]] static TArray<uint8> EncodeFrame(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload);

    /**
     * @brief 从二进制切片首部反解 16 字节小端序报文头
     * @param WireBytes 原始字节视图
     * @return 字节数达到 16 时返回包含报文头的成功态，不足 16 字节时返回对应失败态
     */
    [[nodiscard]] static TGameStatusResult<FNetPacketHeader> TryDecodeHeader(TConstArrayView<uint8> WireBytes);
};

