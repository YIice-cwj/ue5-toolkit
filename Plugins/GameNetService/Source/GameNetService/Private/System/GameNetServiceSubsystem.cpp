// Copyright Epic Games, Inc. All Rights Reserved.

#include "System/GameNetServiceSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Logging/GameLogCategories.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Remote/GameRemoteLogTypes.h"
#include "Serializer/JsonNetSerializer.h"
#include "Transport/NetTransportFactory.h"

/**
 * @brief 子系统初始化：默认装配单机内存回环驱动与 JSON 序列化器，并挂载消息总线桥接器
 * @param Collection 子系统依赖集合
 */
void UGameNetServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    Collection.InitializeDependency<UGameplayMessageSubsystem>();

    ActiveSerializer = MakeShared<FJsonNetSerializer>();
    SetTransport(FNetTransportFactory::CreateMockTransport());

    UGameInstance* OwningGameInstance = GetGameInstance();
    UGameplayMessageSubsystem* MessageBus = (OwningGameInstance != nullptr)
        ? OwningGameInstance->GetSubsystem<UGameplayMessageSubsystem>()
        : nullptr;

    if (MessageBus != nullptr) {
        TWeakObjectPtr<UGameNetServiceSubsystem> WeakThis(this);
        auto OnOutboundMessage = [WeakThis](const FGameNetOutboundMessage& Message) {
            UGameNetServiceSubsystem* Self = WeakThis.Get();
            if (Self != nullptr) {
                Self->SendRaw(Message.MessageId, Message.Payload, Message.SequenceId);
            }
        };

        auto OnRemoteLogEntry = [WeakThis]([[maybe_unused]] FGameplayTag Channel, const FGameRemoteLogEntry& Entry) {
            UGameNetServiceSubsystem* Self = WeakThis.Get();
            if (Self != nullptr && Self->bRemoteLogForwardingEnabled && Self->IsConnected()) {
                Self->SendStruct<FGameRemoteLogEntry>(NET_MSG_REMOTE_LOG, Entry);
            }
        };

        MessageBusBridge.Bind(*MessageBus, MoveTemp(OnOutboundMessage));
        RemoteLogListenerHandle = MessageBus->RegisterListener<FGameRemoteLogEntry>(
            TAG_Game_Log_Remote,
            MoveTemp(OnRemoteLogEntry)
        );
    }

    CoreTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UGameNetServiceSubsystem::HandleCoreTicker),
        0.0f
    );
}

/**
 * @brief 子系统销毁：注销轮询、解绑消息总线、断开连接并安全结算全部挂起请求
 */
void UGameNetServiceSubsystem::Deinitialize() {
    if (CoreTickerHandle.IsValid()) {
        FTSTicker::GetCoreTicker().RemoveTicker(CoreTickerHandle);
        CoreTickerHandle.Reset();
    }

    MessageBusBridge.Unbind();
    RemoteLogListenerHandle.Unregister();
    bManualDisconnect = true;
    bInReconnectCycle = false;
    ReconnectPolicy.Reset();
    HeartbeatKeeper.Reset();
    PendingRequestTracker.FailAll(ENetRequestResult::ConnectionClosed);
    MessageDispatcher.ClearHandlers();

    if (ActiveTransport.IsValid()) {
        UnbindActiveTransportEvents();
        ActiveTransport->Disconnect(1000, TEXT("SubsystemDeinitialize"));
        ActiveTransport.Reset();
    }
    ActiveSerializer.Reset();

    Super::Deinitialize();
}

/**
 * @brief 注入或热切换底层抽象传输驱动实例（切换前自动安全断开旧驱动并结算未决请求）
 * @param NewTransport 新传输驱动共享指针
 */
void UGameNetServiceSubsystem::SetTransport(TSharedPtr<INetTransport> NewTransport) {
    if (ActiveTransport == NewTransport) {
        return;
    }

    bManualDisconnect = true;
    bInReconnectCycle = false;
    ReconnectPolicy.Reset();
    HeartbeatKeeper.Reset();
    PendingRequestTracker.FailAll(ENetRequestResult::ConnectionClosed);

    if (ActiveTransport.IsValid()) {
        UnbindActiveTransportEvents();
        ActiveTransport->Disconnect(1000, TEXT("SwitchTransport"));
    }

    ActiveTransport = MoveTemp(NewTransport);
    BindActiveTransportEvents();
}

/**
 * @brief 注入或热切换网络负载序列化适配器
 * @param NewSerializer 新序列化器共享指针
 */
void UGameNetServiceSubsystem::SetSerializer(TSharedPtr<INetSerializer> NewSerializer) {
    if (NewSerializer.IsValid()) {
        ActiveSerializer = MoveTemp(NewSerializer);
    }
}

/**
 * @brief 配置指数退避自动重连策略
 * @param InConfig 重连策略配置参数
 * @param RandomSeed 随机抖动种子
 */
void UGameNetServiceSubsystem::SetReconnectConfig(const FNetReconnectConfig& InConfig, int32 RandomSeed) {
    ReconnectPolicy = FNetReconnectPolicy(InConfig, RandomSeed);
}

/**
 * @brief 配置周期心跳保活与对端失步超时阈值
 * @param InConfig 心跳配置参数
 */
void UGameNetServiceSubsystem::SetHeartbeatConfig(const FNetHeartbeatConfig& InConfig) {
    HeartbeatKeeper.SetConfig(InConfig);
}

/**
 * @brief 发起向目标服务端（或单机回环虚拟地址）的连接
 * @param Endpoint 目标连接地址
 * @return 成功触发连接流程返回 true
 */
bool UGameNetServiceSubsystem::Connect(const FString& Endpoint) {
    if (!ActiveTransport.IsValid() || Endpoint.IsEmpty()) {
        return false;
    }

    bManualDisconnect = false;
    bInReconnectCycle = false;
    TargetEndpoint = Endpoint;
    ReconnectPolicy.Reset();
    HeartbeatKeeper.Reset();
    return ActiveTransport->Connect(Endpoint).IsSuccess();
}

/**
 * @brief 主动断开当前连接并停止自动重连循环
 * @param ReasonCode 关闭状态码
 * @param ReasonDetail 关闭原因描述
 */
void UGameNetServiceSubsystem::Disconnect(int32 ReasonCode, const FString& ReasonDetail) {
    bManualDisconnect = true;
    const bool bWasReconnecting = bInReconnectCycle;
    bInReconnectCycle = false;
    TargetEndpoint.Reset();
    ReconnectPolicy.Reset();
    HeartbeatKeeper.Reset();
    PendingRequestTracker.FailAll(ENetRequestResult::ConnectionClosed);

    if (ActiveTransport.IsValid()) {
        ActiveTransport->Disconnect(ReasonCode, ReasonDetail);
    }
    if (bWasReconnecting) {
        MessageBusBridge.BroadcastConnectionState(ENetConnectionState::Disconnected, TargetEndpoint);
    }
}

/**
 * @brief 查询当前对外综合连接状态（退避等待重连期间返回 Reconnecting）
 * @return 连接状态枚举
 */
ENetConnectionState UGameNetServiceSubsystem::GetConnectionState() const noexcept {
    if (bInReconnectCycle) {
        return ENetConnectionState::Reconnecting;
    }
    return ActiveTransport.IsValid() ? ActiveTransport->GetState() : ENetConnectionState::Disconnected;
}

/**
 * @brief 查询当前是否处于已连接可发包状态
 * @return 已连接返回 true
 */
bool UGameNetServiceSubsystem::IsConnected() const noexcept {
    return ActiveTransport.IsValid() && ActiveTransport->GetState() == ENetConnectionState::Connected;
}

/**
 * @brief 发送原始二进制负载数据包
 * @param MessageId 业务协议 ID
 * @param Payload 负载二进制切片
 * @param SequenceId 序列号
 * @return 写入发送通道成功返回 Success，失败返回 Failure
 */
TGameStatusResult<void> UGameNetServiceSubsystem::SendRaw(uint32 MessageId, TConstArrayView<uint8> Payload, uint32 SequenceId) {
    if (!IsConnected()) {
        return TGameStatusResult<void>::Failure(Game::NetErrorCodes::NotConnected, TEXT("Not connected"));
    }

    FNetPacketHeader Header;
    Header.MessageId = MessageId;
    Header.SequenceId = SequenceId;
    Header.PayloadLength = static_cast<uint32>(Payload.Num());
    return ActiveTransport->Send(Header, Payload);
}

/**
 * @brief 发送带超时与序列号自动关联的异步原始 Request-Response 请求
 * @param RequestMessageId 请求协议 ID
 * @param ExpectedResponseMessageId 期望匹配的响应协议 ID
 * @param Payload 请求负载切片
 * @param TimeoutSeconds 超时秒数
 * @param Callback 请求完成、断线或超时回调
 * @return 成功返回分配的 SequenceId，失败返回 Failure
 */
TGameStatusResult<uint32> UGameNetServiceSubsystem::SendRawRequest(
    uint32 RequestMessageId,
    uint32 ExpectedResponseMessageId,
    TConstArrayView<uint8> Payload,
    float TimeoutSeconds,
    FNetPendingResponseHandler Callback
) {
    if (!Callback) {
        return TGameStatusResult<uint32>::Failure(Game::CommonErrorCodes::InvalidParameter, TEXT("Invalid callback"));
    }

    const FNetPacketHeader EmptyHeader;
    if (!IsConnected()) {
        Callback(ENetRequestResult::SendFailed, EmptyHeader, {});
        return TGameStatusResult<uint32>::Failure(Game::NetErrorCodes::NotConnected, TEXT("Not connected"));
    }

    const uint32 SequenceId = PendingRequestTracker.AllocateSequenceId();
    const TGameStatusResult<void> SendResult = SendRaw(RequestMessageId, Payload, SequenceId);
    if (SendResult.IsFailure()) {
        Callback(ENetRequestResult::SendFailed, EmptyHeader, {});
        return TGameStatusResult<uint32>::Failure(SendResult.GetError().ErrorCode, SendResult.GetError().Message);
    }

    PendingRequestTracker.RegisterPendingRequest(
        SequenceId,
        ExpectedResponseMessageId,
        TimeoutSeconds,
        MoveTemp(Callback)
    );
    return TGameStatusResult<uint32>::Success(SequenceId);
}

/**
 * @brief 注册指定协议 ID 的原始二进制下行消息处理器
 * @param MessageId 监听的协议 ID
 * @param Handler 到达处理闭包
 * @return 处理器唯一注销句柄 ID
 */
int64 UGameNetServiceSubsystem::RegisterRawHandler(uint32 MessageId, FNetRawMessageHandler Handler) {
    return MessageDispatcher.RegisterHandler(MessageId, MoveTemp(Handler));
}

/**
 * @brief 注销指定协议 ID 的消息处理器
 * @param MessageId 协议 ID
 * @param HandlerId 注册时返回的句柄 ID
 * @return 成功移除返回 Success，失败返回 Failure
 */
TGameStatusResult<void> UGameNetServiceSubsystem::UnregisterHandler(uint32 MessageId, int64 HandlerId) {
    return MessageDispatcher.UnregisterHandler(MessageId, HandlerId);
}

/**
 * @brief 驱动网络队列派发、请求超时扫描、心跳探针与退避重连状态机
 * @param DeltaTimeSeconds 步进秒数
 */
void UGameNetServiceSubsystem::Tick(float DeltaTimeSeconds) {
    if (DeltaTimeSeconds > 0.0f) {
        MonotonicClockSeconds += static_cast<double>(DeltaTimeSeconds);
    }

    if (ActiveTransport.IsValid()) {
        ActiveTransport->Tick(DeltaTimeSeconds);
    }

    PendingRequestTracker.TickTimeouts(DeltaTimeSeconds);

    if (IsConnected()) {
        const ENetHeartbeatAction Action = HeartbeatKeeper.Tick(DeltaTimeSeconds);
        if (Action == ENetHeartbeatAction::SendPing) {
            const uint32 PingSeq = PendingRequestTracker.AllocateSequenceId();
            if (SendRaw(HeartbeatKeeper.GetConfig().PingMessageId, {}, PingSeq).IsSuccess()) {
                HeartbeatKeeper.RecordPingSent(PingSeq, MonotonicClockSeconds);
            }
        } else if (Action == ENetHeartbeatAction::TimeoutDisconnect) {
            GAME_LOG(Warning, TEXT("【GameNetService】心跳静默超时，主动断开并尝试重连。"));
            ActiveTransport->Disconnect(1001, TEXT("HeartbeatTimeout"));
        }
        return;
    }

    if (bInReconnectCycle && ReconnectPolicy.IsWaitingForRetry() && ActiveTransport.IsValid()) {
        if (ReconnectPolicy.AdvanceTimer(DeltaTimeSeconds)) {
            const bool bStarted = ActiveTransport->Connect(TargetEndpoint).IsSuccess();
            if (!bStarted && !ReconnectPolicy.IsWaitingForRetry() && !ReconnectPolicy.IsExhausted()) {
                TryEnterReconnectCycle();
            }
        }
    }
}

/**
 * @brief 安全获取业务网络门面子系统指针（可空）
 * @param WorldContextObject 世界上下文对象
 * @return 子系统实例指针，未就绪时返回 nullptr
 */
UGameNetServiceSubsystem* UGameNetServiceSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr || GEngine == nullptr) {
        return nullptr;
    }
    if (const UGameInstance* DirectInstance = Cast<UGameInstance>(WorldContextObject)) {
        return DirectInstance->GetSubsystem<UGameNetServiceSubsystem>();
    }
    const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (World == nullptr || World->GetGameInstance() == nullptr) {
        return nullptr;
    }
    return World->GetGameInstance()->GetSubsystem<UGameNetServiceSubsystem>();
}

/**
 * @brief 获取业务网络门面子系统引用（断言有效）
 * @param WorldContextObject 世界上下文对象
 * @return 子系统引用
 */
UGameNetServiceSubsystem& UGameNetServiceSubsystem::Get(const UObject* WorldContextObject) {
    UGameNetServiceSubsystem* Subsystem = GetPtr(WorldContextObject);
    check(Subsystem != nullptr);
    return *Subsystem;
}

/**
 * @brief 开启或关闭对总线远程日志的自动网络转送实现
 * @param bEnabled 是否开启
 */
void UGameNetServiceSubsystem::SetRemoteLogForwardingEnabled(bool bEnabled) {
    bRemoteLogForwardingEnabled = bEnabled;
}

/**
 * @brief 查询当前是否开启了对总线远程日志的自动网络转送实现
 * @return 是否开启
 */
bool UGameNetServiceSubsystem::IsRemoteLogForwardingEnabled() const noexcept {
    return bRemoteLogForwardingEnabled;
}

/**
 * @brief CoreTicker 帧回调桥接入口
 * @param DeltaTimeSeconds 帧间隔秒数
 * @return 始终返回 true 以保持常驻周期调度
 */
bool UGameNetServiceSubsystem::HandleCoreTicker(float DeltaTimeSeconds) {
    Tick(DeltaTimeSeconds);
    return true;
}

/**
 * @brief 绑定当前活跃传输驱动的状态与收包委托
 */
void UGameNetServiceSubsystem::BindActiveTransportEvents() {
    if (!ActiveTransport.IsValid()) {
        return;
    }

    TransportStateHandle = ActiveTransport->OnStateChanged().AddUObject(
        this,
        &UGameNetServiceSubsystem::HandleTransportStateChanged
    );
    TransportPacketHandle = ActiveTransport->OnPacketReceived().AddUObject(
        this,
        &UGameNetServiceSubsystem::HandleTransportPacketReceived
    );
}

/**
 * @brief 解绑当前活跃传输驱动的全部委托
 */
void UGameNetServiceSubsystem::UnbindActiveTransportEvents() {
    if (!ActiveTransport.IsValid()) {
        return;
    }

    if (TransportStateHandle.IsValid()) {
        ActiveTransport->OnStateChanged().Remove(TransportStateHandle);
        TransportStateHandle.Reset();
    }
    if (TransportPacketHandle.IsValid()) {
        ActiveTransport->OnPacketReceived().Remove(TransportPacketHandle);
        TransportPacketHandle.Reset();
    }
}

/**
 * @brief 处理底层传输驱动状态变更，联动异步请求清理、重连调度与消息总线桥接广播
 * @param NewTransportState 底层驱动最新状态
 */
void UGameNetServiceSubsystem::HandleTransportStateChanged(ENetConnectionState NewTransportState) {
    if (NewTransportState == ENetConnectionState::Connected) {
        bInReconnectCycle = false;
        ReconnectPolicy.Reset();
        HeartbeatKeeper.Reset();
        MessageBusBridge.BroadcastConnectionState(ENetConnectionState::Connected, TargetEndpoint);
        return;
    }

    if (NewTransportState == ENetConnectionState::Disconnected) {
        HeartbeatKeeper.Reset();
        PendingRequestTracker.FailAll(ENetRequestResult::ConnectionClosed);

        if (!bManualDisconnect && !TargetEndpoint.IsEmpty()) {
            TryEnterReconnectCycle();
            return;
        }
        bInReconnectCycle = false;
        MessageBusBridge.BroadcastConnectionState(ENetConnectionState::Disconnected, TargetEndpoint);
        return;
    }

    if (!bInReconnectCycle) {
        MessageBusBridge.BroadcastConnectionState(NewTransportState, TargetEndpoint);
    }
}

/**
 * @brief 处理底层传输驱动收到的完整数据包，更新心跳、匹配异步响应、多播路由并转发至消息总线
 * @param Header 报文头
 * @param Payload 负载切片
 */
void UGameNetServiceSubsystem::HandleTransportPacketReceived(
    const FNetPacketHeader& Header,
    TConstArrayView<uint8> Payload
) {
    HeartbeatKeeper.NotifyInboundPacket(Header.MessageId, Header.SequenceId, MonotonicClockSeconds);
    PendingRequestTracker.TryCompleteResponse(Header, Payload);
    MessageDispatcher.DispatchPacket(Header, Payload);
    MessageBusBridge.BroadcastInboundPacket(Header, Payload);
}

/**
 * @brief 在意外断线后尝试开启下一轮指数退避重连周期
 */
void UGameNetServiceSubsystem::TryEnterReconnectCycle() {
    const TOptional<float> ScheduledSeconds = ReconnectPolicy.ScheduleNextAttempt();
    if (ScheduledSeconds.IsSet()) {
        const bool bWasAlreadyReconnecting = bInReconnectCycle;
        bInReconnectCycle = true;
        if (!bWasAlreadyReconnecting) {
            MessageBusBridge.BroadcastConnectionState(ENetConnectionState::Reconnecting, TargetEndpoint);
        }
        return;
    }

    bInReconnectCycle = false;
    MessageBusBridge.BroadcastConnectionState(ENetConnectionState::Disconnected, TargetEndpoint);
}

