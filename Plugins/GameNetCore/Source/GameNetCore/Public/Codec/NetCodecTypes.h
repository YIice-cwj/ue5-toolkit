/**
 * @file NetCodecTypes.h
 * @brief 二进制报文帧流解析状态枚举与解包结果值对象定义
 */

#pragma once

#include "CoreMinimal.h"
#include "Packet/NetPacketTypes.h"

#include "NetCodecTypes.generated.h"

/**
 * @brief 二进制网络报文帧流解析结果状态枚举
 */
UENUM(BlueprintType)
enum class ENetFrameParseStatus : uint8 {
    CompleteFrame = 0 UMETA(DisplayName = "CompleteFrame"),
    NeedMoreData UMETA(DisplayName = "NeedMoreData"),
    InvalidMagic UMETA(DisplayName = "InvalidMagic"),
    InvalidHeaderSize UMETA(DisplayName = "InvalidHeaderSize"),
    PayloadExceedsLimit UMETA(DisplayName = "PayloadExceedsLimit")
};

/**
 * @brief 流缓冲区单次解包结果值对象
 */
struct GAMENETCORE_API FNetFrameDecodeOutcome {
    ENetFrameParseStatus Status{ENetFrameParseStatus::NeedMoreData};
    FNetPacket Packet;
};

