/**
 * @file NetMessageBusBridge.h
 * @brief 网络服务与 GameCore GameplayMessageSubsystem 解耦事件总线的双向桥接器
 */

#pragma once

#include "CoreMinimal.h"
#include "Message/GameNetworkMessageTypes.h"
#include "Message/GameplayMessageTypes.h"
#include "Packet/NetPacketTypes.h"
#include "Types/NetConnectionTypes.h"

class UGameplayMessageSubsystem;

/**
 * @brief 订阅总线外发请求并广播下行报文与连接状态迁移事件的桥接组件
 */
class GAMENETSERVICE_API FNetMessageBusBridge final {
public:
    using FOutboundRequestCallback = TFunction<void(const FGameNetOutboundMessage&)>;

public:
    FNetMessageBusBridge() = default;

    /**
     * @brief 析构时自动注销消息总线监听句柄
     */
    ~FNetMessageBusBridge();

    FNetMessageBusBridge(const FNetMessageBusBridge&) = delete;
    FNetMessageBusBridge& operator=(const FNetMessageBusBridge&) = delete;
    FNetMessageBusBridge(FNetMessageBusBridge&&) = delete;
    FNetMessageBusBridge& operator=(FNetMessageBusBridge&&) = delete;

    /**
     * @brief 挂载至指定 GameplayMessageSubsystem 并开始监听 TAG_Game_Net_Outbound 通道
     * @param MessageBus 目标消息总线子系统引用
     * @param OnOutboundRequested 收到外发请求信封时的转发回调闭包
     */
    void Bind(UGameplayMessageSubsystem& MessageBus, FOutboundRequestCallback OnOutboundRequested);

    /**
     * @brief 解绑消息总线监听句柄并清空总线弱引用
     */
    void Unbind();

    /**
     * @brief 查询桥接器当前是否已绑定有效消息总线
     * @return 已绑定有效总线返回 true
     */
    [[nodiscard]] bool IsBound() const noexcept {
        return BoundMessageBus.IsValid();
    }

    /**
     * @brief 向 TAG_Game_Net_Inbound 通道广播一条下行到达网络数据包
     * @param Header 到达报文头
     * @param Payload 到达负载二进制切片
     */
    void BroadcastInboundPacket(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload) const;

    /**
     * @brief 向 TAG_Game_Net_StateChanged 通道广播当前网络连接状态迁移通知
     * @param EffectiveState 当前综合连接状态
     * @param Endpoint 当前关联的目标端点地址
     */
    void BroadcastConnectionState(ENetConnectionState EffectiveState, const FString& Endpoint) const;

private:
    TWeakObjectPtr<UGameplayMessageSubsystem> BoundMessageBus;
    FGameplayMessageListenerHandle OutboundListenerHandle;
};

