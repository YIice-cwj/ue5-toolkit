/**
 * @file GameNetworkMessageTypes.h
 * @brief 网络事件总线通道标签与通用解耦信封数据契约（基于 GameMessage GameplayMessageSubsystem 广播互通）
 */

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"
#include "Types/NetConnectionTypes.h"

#include "GameNetworkMessageTypes.generated.h"

GAMENETCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Game_Net_Outbound);
GAMENETCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Game_Net_Inbound);
GAMENETCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Game_Net_StateChanged);

inline constexpr uint32 NET_MSG_HEARTBEAT_PING = 1;
inline constexpr uint32 NET_MSG_HEARTBEAT_PONG = 2;

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
    FGameNetOutboundMessage() = default;

    /**
     * @brief 带参便捷构造上行信封
     * @param InMessageId 协议 ID
     * @param InSequenceId 序列号
     * @param InPayload 负载字节切片
     */
    FGameNetOutboundMessage(uint32 InMessageId, uint32 InSequenceId, TConstArrayView<uint8> InPayload = {})
        : MessageId(InMessageId)
        , SequenceId(InSequenceId) {
        if (!InPayload.IsEmpty()) {
            Payload.SetNumUninitialized(InPayload.Num());
            FMemory::Memcpy(Payload.GetData(), InPayload.GetData(), InPayload.Num());
        }
    }

    /**
     * @brief 相等性比较运算符
     * @param Other 待比较的另一信封
     * @return 所有字段均相等时返回 true
     */
    [[nodiscard]] bool operator==(const FGameNetOutboundMessage& Other) const noexcept {
        return MessageId == Other.MessageId && SequenceId == Other.SequenceId && Payload == Other.Payload;
    }

    /**
     * @brief 清空信封数据并复用负载预留容量
     */
    void Reset() noexcept {
        MessageId = 0;
        SequenceId = 0;
        Payload.Reset();
    }

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
    FGameNetInboundMessage() = default;

    /**
     * @brief 带参便捷构造下行信封
     * @param InMessageId 协议 ID
     * @param InSequenceId 序列号
     * @param InPayload 负载字节切片
     */
    FGameNetInboundMessage(uint32 InMessageId, uint32 InSequenceId, TConstArrayView<uint8> InPayload = {})
        : MessageId(InMessageId)
        , SequenceId(InSequenceId) {
        if (!InPayload.IsEmpty()) {
            Payload.SetNumUninitialized(InPayload.Num());
            FMemory::Memcpy(Payload.GetData(), InPayload.GetData(), InPayload.Num());
        }
    }

    /**
     * @brief 相等性比较运算符
     * @param Other 待比较的另一信封
     * @return 所有字段均相等时返回 true
     */
    [[nodiscard]] bool operator==(const FGameNetInboundMessage& Other) const noexcept {
        return MessageId == Other.MessageId && SequenceId == Other.SequenceId && Payload == Other.Payload;
    }

    /**
     * @brief 清空信封数据并复用负载预留容量
     */
    void Reset() noexcept {
        MessageId = 0;
        SequenceId = 0;
        Payload.Reset();
    }

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
    FGameNetStateMessage() = default;

    /**
     * @brief 带参便捷构造连接状态信封
     * @param bInIsConnected 是否已连接
     * @param InState 连接状态枚举
     * @param InEndpoint 端点地址
     */
    FGameNetStateMessage(bool bInIsConnected, ENetConnectionState InState, FString InEndpoint = {})
        : bIsConnected(bInIsConnected)
        , ConnectionState(InState)
        , Endpoint(MoveTemp(InEndpoint)) {
    }

    /**
     * @brief 相等性比较运算符
     * @param Other 待比较的另一状态信封
     * @return 所有字段均相等时返回 true
     */
    [[nodiscard]] bool operator==(const FGameNetStateMessage& Other) const noexcept {
        return bIsConnected == Other.bIsConnected && ConnectionState == Other.ConnectionState && Endpoint == Other.Endpoint;
    }

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|MessageBus")
    bool bIsConnected{false};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|MessageBus")
    ENetConnectionState ConnectionState{ENetConnectionState::Disconnected};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|MessageBus")
    FString Endpoint;
};

