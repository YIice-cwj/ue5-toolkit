/**
 * @file LanBeaconTypes.h
 * @brief 局域网 UDP 信标接收数据报值对象与房间发现/过期多播事件委托契约
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/GameNetSessionTypes.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnLanRoomDiscovered, const FGameNetRoomDescriptor&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnLanRoomExpired, const FString&);

/**
 * @brief 从非阻塞局域网 UDP 通道读取的单帧原始数据报与发送方端点信息
 */
struct GAMENETSESSION_API FLanReceivedDatagram {
    TArray<uint8> Bytes;
    FString SenderIp;
    int32 SenderPort{0};
};

