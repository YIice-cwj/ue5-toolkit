/**
 * @file NetDispatchTypes.h
 * @brief 业务网络报文路由与异步请求结算状态枚举及回调类型别名契约
 */

#pragma once

#include "CoreMinimal.h"
#include "Packet/NetPacketTypes.h"

#include "NetDispatchTypes.generated.h"

/**
 * @brief 异步网络请求（Request-Response）完成状态枚举
 */
UENUM(BlueprintType)
enum class ENetRequestResult : uint8 {
    Success = 0 UMETA(DisplayName = "Success"),
    TimedOut UMETA(DisplayName = "TimedOut"),
    ConnectionClosed UMETA(DisplayName = "ConnectionClosed"),
    SendFailed UMETA(DisplayName = "SendFailed"),
    DeserializeFailed UMETA(DisplayName = "DeserializeFailed")
};

using FNetRawMessageHandler = TFunction<void(const FNetPacketHeader&, TConstArrayView<uint8>)>;
using FNetPendingResponseHandler = TFunction<void(ENetRequestResult, const FNetPacketHeader&, TConstArrayView<uint8>)>;

/**
 * @brief 业务网络错误码常量定义
 */
namespace Game::NetErrorCodes {
    inline constexpr int32 NotConnected = 2002;
    inline constexpr int32 InvalidSerializer = 2004;
    inline constexpr int32 HandlerNotFound = 2005;
    inline constexpr int32 NoHandlersRegistered = 2006;
}

