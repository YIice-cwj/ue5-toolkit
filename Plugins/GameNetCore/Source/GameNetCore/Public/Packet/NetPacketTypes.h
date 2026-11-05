/**
 * @file NetPacketTypes.h
 * @brief 通用网络报文头与完整数据包纯数据契约定义
 */

#pragma once

#include "CoreMinimal.h"

#include "NetPacketTypes.generated.h"

/**
 * @brief 通用网络报文头数据契约（对应固定 16 字节小端序线格式字段）
 */
USTRUCT(BlueprintType)
struct GAMENETCORE_API FNetPacketHeader {
    GENERATED_BODY()

public:
    static constexpr uint16 PACKET_MAGIC = 0x5545;
    static constexpr uint16 WIRE_HEADER_BYTES = 16;
    static constexpr uint32 DEFAULT_MAX_PAYLOAD_BYTES = 2 * 1024 * 1024;

public:
    /**
     * @brief 相等性比较运算符
     * @param Other 待比较的另一报文头
     * @return 所有字段均相等时返回 true
     */
    [[nodiscard]] bool operator==(const FNetPacketHeader& Other) const noexcept = default;

public:
    UPROPERTY()
    uint16 Magic{PACKET_MAGIC};

    UPROPERTY()
    uint16 HeaderSize{WIRE_HEADER_BYTES};

    UPROPERTY()
    uint32 PayloadLength{0};

    UPROPERTY()
    uint32 MessageId{0};

    UPROPERTY()
    uint32 SequenceId{0};
};

/**
 * @brief 完整网络数据包值对象（包含报文头与负载字节数组）
 */
USTRUCT(BlueprintType)
struct GAMENETCORE_API FNetPacket {
    GENERATED_BODY()

public:
    UPROPERTY()
    FNetPacketHeader Header;

    UPROPERTY()
    TArray<uint8> Payload;
};

