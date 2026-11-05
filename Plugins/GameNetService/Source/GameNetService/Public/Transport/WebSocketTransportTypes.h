/**
 * @file WebSocketTransportTypes.h
 * @brief WebSocket 分片组装结果状态与底层实例工厂函数类型契约
 */

#pragma once

#include "CoreMinimal.h"

class IWebSocket;

/**
 * @brief WebSocket 二进制消息分片拼装状态枚举
 */
enum class EWebSocketFragmentStatus : uint8 {
    Incomplete = 0,
    MessageReady,
    FrameExceedsLimit
};

/**
 * @brief WebSocket 单次分片追加后的组装结果值对象
 */
struct GAMENETSERVICE_API FWebSocketFragmentOutcome {
    EWebSocketFragmentStatus Status{EWebSocketFragmentStatus::Incomplete};
    TArray<uint8> AssembledMessage;
};

using FWebSocketFactory = TFunction<TSharedPtr<IWebSocket>(
    const FString&,
    const FString&,
    const TMap<FString, FString>&
)>;

