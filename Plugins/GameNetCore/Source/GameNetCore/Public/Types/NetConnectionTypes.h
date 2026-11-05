/**
 * @file NetConnectionTypes.h
 * @brief 网络传输通道连接生命周期状态枚举定义
 */

#pragma once

#include "CoreMinimal.h"

#include "NetConnectionTypes.generated.h"

/**
 * @brief 业务网络连接生命周期状态枚举
 */
UENUM(BlueprintType)
enum class ENetConnectionState : uint8 {
    Disconnected = 0 UMETA(DisplayName = "Disconnected"),
    Connecting UMETA(DisplayName = "Connecting"),
    Connected UMETA(DisplayName = "Connected"),
    Reconnecting UMETA(DisplayName = "Reconnecting"),
    Closing UMETA(DisplayName = "Closing")
};

