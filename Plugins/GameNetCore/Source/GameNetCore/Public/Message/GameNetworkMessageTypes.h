/**
 * @file GameNetworkMessageTypes.h
 * @brief 网络事件总线通道标签与通用解耦信封数据契约（基于 GameCore GameplayMessageSubsystem 广播互通）
 */

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"
#include "Types/NetConnectionTypes.h"

#include "GameNetworkMessageTypes.generated.h"

GAMENETCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Game_Net_Outbound);
GAMENETCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Game_Net_Inbound);
GAMENETCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Game_Net_StateChanged);

inline constexpr uint32 NET_MSG_REMOTE_LOG = 100;

inline constexpr uint32 NET_MSG_CLOUD_SAVE_UPLOAD_REQ = 201;
inline constexpr uint32 NET_MSG_CLOUD_SAVE_UPLOAD_RSP = 202;
inline constexpr uint32 NET_MSG_CLOUD_SAVE_DOWNLOAD_REQ = 203;
inline constexpr uint32 NET_MSG_CLOUD_SAVE_DOWNLOAD_RSP = 204;
inline constexpr uint32 NET_MSG_CLOUD_SAVE_QUERY_REQ = 205;
inline constexpr uint32 NET_MSG_CLOUD_SAVE_QUERY_RSP = 206;

/**
 * @brief 业务插件通过 GameplayMessageSubsystem 请求外发网络报文的解耦信封载荷
 */
USTRUCT(BlueprintType)
struct GAMENETCORE_API FGameNetOutboundMessage {
    GENERATED_BODY()

public:
    UPROPERTY()
    uint32 MessageId{0};

    UPROPERTY()
    uint32 SequenceId{0};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|MessageBus")
    TArray<uint8> Payload;
};

/**
 * @brief 网络服务向 GameplayMessageSubsystem 广播下行到达报文的解耦信封载荷
 */
USTRUCT(BlueprintType)
struct GAMENETCORE_API FGameNetInboundMessage {
    GENERATED_BODY()

public:
    UPROPERTY()
    uint32 MessageId{0};

    UPROPERTY()
    uint32 SequenceId{0};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|MessageBus")
    TArray<uint8> Payload;
};

/**
 * @brief 网络服务向 GameplayMessageSubsystem 广播连接状态迁移的解耦通知载荷
 */
USTRUCT(BlueprintType)
struct GAMENETCORE_API FGameNetStateMessage {
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|MessageBus")
    bool bIsConnected{false};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|MessageBus")
    ENetConnectionState ConnectionState{ENetConnectionState::Disconnected};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|MessageBus")
    FString Endpoint;
};

