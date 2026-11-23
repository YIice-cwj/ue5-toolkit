// Copyright Epic Games, Inc. All Rights Reserved.

#include "Bridge/NetMessageBusBridge.h"

#include "Message/GameplayMessageSubsystem.h"

/**
 * @brief 析构时自动注销消息总线监听句柄
 */
FNetMessageBusBridge::~FNetMessageBusBridge() {
    Unbind();
}

/**
 * @brief 挂载至指定 GameplayMessageSubsystem 并开始监听 TAG_Game_Net_Outbound 通道
 * @param MessageBus 目标消息总线子系统引用
 * @param OnOutboundRequested 收到外发请求信封时的转发回调闭包
 */
void FNetMessageBusBridge::Bind(
    UGameplayMessageSubsystem& MessageBus,
    FOutboundRequestCallback OnOutboundRequested
) {
    Unbind();
    if (!OnOutboundRequested) {
        return;
    }

    BoundMessageBus = &MessageBus;
    OutboundListenerHandle = MessageBus.RegisterListener<FGameNetOutboundMessage>(
        TAG_Game_Net_Outbound,
        MoveTemp(OnOutboundRequested)
    );
}

/**
 * @brief 解绑消息总线监听句柄并清空总线弱引用
 */
void FNetMessageBusBridge::Unbind() {
    OutboundListenerHandle.Unregister();
    BoundMessageBus.Reset();
}

/**
 * @brief 向 TAG_Game_Net_Inbound 通道广播一条下行到达网络数据包（无监听者时零分配短路）
 * @param Header 到达报文头
 * @param Payload 到达负载二进制切片
 */
void FNetMessageBusBridge::BroadcastInboundPacket(
    const FNetPacketHeader& Header,
    TConstArrayView<uint8> Payload
) const {
    UGameplayMessageSubsystem* MessageBus = BoundMessageBus.Get();
    if (MessageBus == nullptr || !MessageBus->HasListener(TAG_Game_Net_Inbound)) {
        return;
    }

    const FGameNetInboundMessage BusEnvelope(Header.MessageId, Header.SequenceId, Payload);
    (void)MessageBus->BroadcastMessage(TAG_Game_Net_Inbound, BusEnvelope);
}

/**
 * @brief 向 TAG_Game_Net_StateChanged 通道广播当前网络连接状态迁移通知（无监听者时零分配短路）
 * @param EffectiveState 当前综合连接状态
 * @param Endpoint 当前关联的目标端点地址
 */
void FNetMessageBusBridge::BroadcastConnectionState(
    ENetConnectionState EffectiveState,
    const FString& Endpoint
) const {
    UGameplayMessageSubsystem* MessageBus = BoundMessageBus.Get();
    if (MessageBus == nullptr || !MessageBus->HasListener(TAG_Game_Net_StateChanged)) {
        return;
    }

    const FGameNetStateMessage StateNotice(
        EffectiveState == ENetConnectionState::Connected,
        EffectiveState,
        Endpoint
    );
    (void)MessageBus->BroadcastMessage(TAG_Game_Net_StateChanged, StateNotice);
}
