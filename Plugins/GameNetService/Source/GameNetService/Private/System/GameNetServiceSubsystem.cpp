// Copyright Epic Games, Inc. All Rights Reserved.

#include "System/GameNetServiceSubsystem.h"

#include "Async/FGameThreadTaskRunner.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Logging/GameLogCategories.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Remote/GameRemoteLogTypes.h"
#include "Serializer/JsonNetSerializer.h"
#include "Transport/NetTransportFactory.h"

/**
 * @brief 安全获取业务网络门面子系统指针（支持 UWorld、UGameInstance、UGameInstanceSubsystem 与场景对象）
 * @param WorldContextObject 世界或子系统上下文对象
 * @return 子系统实例指针，未就绪时返回 nullptr
 */
UGameNetServiceSubsystem* UGameNetServiceSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr) {
        return nullptr;
    }
    if (const UGameInstance* DirectInstance = Cast<UGameInstance>(WorldContextObject)) {
        return DirectInstance->GetSubsystem<UGameNetServiceSubsystem>();
    }
    if (const UGameInstanceSubsystem* SiblingSubsystem = Cast<UGameInstanceSubsystem>(WorldContextObject)) {
        if (const UGameInstance* OwningInstance = SiblingSubsystem->GetGameInstance()) {
            return OwningInstance->GetSubsystem<UGameNetServiceSubsystem>();
        }
        return nullptr;
    }
    if (GEngine == nullptr) {
        return nullptr;
    }
    const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (World == nullptr || World->GetGameInstance() == nullptr) {
        return nullptr;
    }
    return World->GetGameInstance()->GetSubsystem<UGameNetServiceSubsystem>();
}

/**
 * @brief 获取业务网络门面子系统引用（断言有效）
 * @param WorldContextObject 世界或子系统上下文对象
 * @return 子系统引用
 */
UGameNetServiceSubsystem& UGameNetServiceSubsystem::Get(const UObject* WorldContextObject) {
    UGameNetServiceSubsystem* Subsystem = GetPtr(WorldContextObject);
    check(Subsystem != nullptr);
    return *Subsystem;
}

/**
 * @brief 子系统初始化：默认装配单机内存回环驱动与 JSON 序列化器，并挂载消息总线桥接器与远程日志监听
 * @param Collection 子系统依赖集合
 */
void UGameNetServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    Collection.InitializeDependency<UGameplayMessageSubsystem>();

    ActiveSerializer = MakeShared<FJsonNetSerializer>();
    SetTransport(FNetTransportFactory::CreateMockTransport());
    BindMessageBusListeners();

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
    TargetEndpoint.Reset();
    ReconnectPolicy.Reset();
    HeartbeatKeeper.Reset();
    MessageDispatcher.ClearHandlers();

    if (ActiveTransport.IsValid()) {
        UnbindActiveTransportEvents();
        TSharedPtr<INetTransport> ClosingTransport = MoveTemp(ActiveTransport);
        ClosingTransport->Disconnect(1000, TEXT("SubsystemDeinitialize"));
    }
    ActiveSerializer.Reset();
    PendingRequestTracker.FailAll(ENetRequestResult::ConnectionClosed);

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
    TargetEndpoint.Reset();
    ReconnectPolicy.Reset();
    HeartbeatKeeper.Reset();

    TSharedPtr<INetTransport> OldTransport;
    if (ActiveTransport.IsValid()) {
        UnbindActiveTransportEvents();
        OldTransport = MoveTemp(ActiveTransport);
    }

    ActiveTransport = MoveTemp(NewTransport);
    BindActiveTransportEvents();

    if (OldTransport.IsValid()) {
        OldTransport->Disconnect(1000, TEXT("SwitchTransport"));
    }
    PendingRequestTracker.FailAll(ENetRequestResult::ConnectionClosed);
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
 * @brief 使用运行期高熵随机种子配置指数退避自动重连策略
 * @param InConfig 重连策略配置参数
 */
void UGameNetServiceSubsystem::SetReconnectConfig(const FNetReconnectConfig& InConfig) {
    ReconnectPolicy.SetConfig(InConfig);
}

/**
 * @brief 使用指定确定性随机种子配置指数退避自动重连策略
 * @param InConfig 重连策略配置参数
 * @param DeterministicSeed 确定性随机种子
 */
void UGameNetServiceSubsystem::SetReconnectConfig(const FNetReconnectConfig& InConfig, uint64 DeterministicSeed) {
    ReconnectPolicy = FNetReconnectPolicy(InConfig, DeterministicSeed);
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
        GAME_LOG(Warning, TEXT("[Service] Connect 被拒：底层传输未装配或端点地址为空。"));
        return false;
    }

    const ENetConnectionState TransportState = ActiveTransport->GetState();
    if (TransportState == ENetConnectionState::Connected || TransportState == ENetConnectionState::Connecting) {
        GAME_LOG(Warning, TEXT("[Service] Connect 被拒：当前已处于连接中或已连接状态 (Target=%s)。"), *TargetEndpoint);
        return false;
    }

    bManualDisconnect = false;
    bInReconnectCycle = false;
    TargetEndpoint = Endpoint;
    ReconnectPolicy.Reset();
    HeartbeatKeeper.Reset();

    const TGameStatusResult<void> ConnectResult = ActiveTransport->Connect(Endpoint);
    if (ConnectResult.IsFailure()) {
        if (!bInReconnectCycle) {
            bManualDisconnect = true;
            TargetEndpoint.Reset();
        }
        GAME_LOG(
            Warning,
            TEXT("[Service] 发起连接失败: Endpoint=%s, Error=%s"),
            *Endpoint,
            *ConnectResult.GetError().ToString()
        );
        return false;
    }
    return true;
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
    ReconnectPolicy.Reset();
    HeartbeatKeeper.Reset();

    const FString PreviousEndpoint = MoveTemp(TargetEndpoint);
    TargetEndpoint.Reset();
    const bool bTransportWasDisconnected =
        !ActiveTransport.IsValid() || ActiveTransport->GetState() == ENetConnectionState::Disconnected;

    if (ActiveTransport.IsValid()) {
        ActiveTransport->Disconnect(ReasonCode, ReasonDetail);
    }
    if (!IsConnected()) {
        PendingRequestTracker.FailAll(ENetRequestResult::ConnectionClosed);
    }
    if (bWasReconnecting && bTransportWasDisconnected && !IsConnected()) {
        MessageBusBridge.BroadcastConnectionState(ENetConnectionState::Disconnected, PreviousEndpoint);
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
 * @brief 发送原始二进制负载数据包
 * @param MessageId 业务协议 ID
 * @param Payload 负载二进制切片
 * @param SequenceId 序列号
 * @return 写入发送通道成功返回 Success，失败返回带 Game::ModuleId::Net 的 Failure
 */
TGameStatusResult<void> UGameNetServiceSubsystem::SendRaw(
    uint32 MessageId,
    TConstArrayView<uint8> Payload,
    uint32 SequenceId
) {
    if (!IsConnected()) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::NotConnected,
            TEXT("Not connected")
        );
    }

    FNetPacketHeader Header;
    Header.MessageId = MessageId;
    Header.SequenceId = SequenceId;
    Header.PayloadLength = static_cast<uint32>(Payload.Num());
    return ActiveTransport->Send(Header, Payload);
}

/**
 * @brief 发送带超时、序列号自动关联与可选协同取消令牌的异步原始 Request-Response 请求
 * @param RequestMessageId 请求协议 ID
 * @param ExpectedResponseMessageId 期望匹配的响应协议 ID
 * @param Payload 请求负载切片
 * @param TimeoutSeconds 超时秒数
 * @param Callback 请求完成、取消、断线或超时回调
 * @param CancellationToken 可选协同取消令牌
 * @return 成功返回分配的 SequenceId，失败返回带 Game::ModuleId::Net 的 Failure
 */
TGameStatusResult<uint32> UGameNetServiceSubsystem::SendRawRequest(
    uint32 RequestMessageId,
    uint32 ExpectedResponseMessageId,
    TConstArrayView<uint8> Payload,
    float TimeoutSeconds,
    FNetPendingResponseHandler Callback,
    FCancellationToken CancellationToken
) {
    if (!Callback) {
        return TGameStatusResult<uint32>::Failure(
            Game::ModuleId::Net,
            Game::CommonErrorCodes::InvalidParameter,
            TEXT("Invalid callback")
        );
    }

    const FNetPacketHeader EmptyHeader;
    if (CancellationToken.IsCancellationRequested()) {
        Callback(ENetRequestResult::Cancelled, EmptyHeader, {});
        return TGameStatusResult<uint32>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::RequestCancelled,
            TEXT("Request cancelled before dispatch")
        );
    }

    if (!IsConnected()) {
        Callback(ENetRequestResult::SendFailed, EmptyHeader, {});
        return TGameStatusResult<uint32>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::NotConnected,
            TEXT("Not connected")
        );
    }

    const uint32 SequenceId = PendingRequestTracker.AllocateSequenceId();
    const TGameStatusResult<void> SendResult = SendRaw(RequestMessageId, Payload, SequenceId);
    if (SendResult.IsFailure()) {
        Callback(ENetRequestResult::SendFailed, EmptyHeader, {});
        return TGameStatusResult<uint32>::Failure(SendResult.GetError());
    }
    if (!IsConnected()) {
        Callback(ENetRequestResult::ConnectionClosed, EmptyHeader, {});
        return TGameStatusResult<uint32>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::NotConnected,
            TEXT("Connection closed during request send")
        );
    }

    PendingRequestTracker.RegisterPendingRequest(
        SequenceId,
        ExpectedResponseMessageId,
        TimeoutSeconds,
        MoveTemp(Callback)
    );

    if (CancellationToken.CanBeCancelled()) {
        TWeakObjectPtr<UGameNetServiceSubsystem> WeakThis(this);
        FCancellationTokenRegistration CancelRegistration = CancellationToken.Register([WeakThis, SequenceId]() {
            if (IsInGameThread()) {
                if (UGameNetServiceSubsystem* Self = WeakThis.Get()) {
                    Self->CancelPendingRequest(SequenceId);
                }
                return;
            }
            FGameThreadTaskRunner::RunOnGameThread([WeakThis, SequenceId]() {
                if (UGameNetServiceSubsystem* Self = WeakThis.Get()) {
                    Self->CancelPendingRequest(SequenceId);
                }
            });
        });

        if (!PendingRequestTracker.AttachCancellationRegistration(SequenceId, MoveTemp(CancelRegistration))) {
            return TGameStatusResult<uint32>::Failure(
                Game::ModuleId::Net,
                Game::NetErrorCodes::RequestCancelled,
                TEXT("Request cancelled during registration")
            );
        }
    }

    return TGameStatusResult<uint32>::Success(SequenceId);
}

/**
 * @brief 主动取消指定序列号的在途异步请求并立即触发 Cancelled 回调
 * @param SequenceId 请求序列号
 * @return 成功命中并取消返回 true
 */
bool UGameNetServiceSubsystem::CancelPendingRequest(uint32 SequenceId) {
    return PendingRequestTracker.CancelRequest(SequenceId);
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
 * @return 成功找到并移除返回 true
 */
bool UGameNetServiceSubsystem::UnregisterHandler(uint32 MessageId, int64 HandlerId) {
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
        TickHeartbeat(DeltaTimeSeconds);
        return;
    }

    TickReconnect(DeltaTimeSeconds);
}

/**
 * @brief 绑定消息总线桥接器与远程日志监听回调
 */
void UGameNetServiceSubsystem::BindMessageBusListeners() {
    UGameInstance* OwningGameInstance = GetGameInstance();
    if (OwningGameInstance == nullptr) {
        return;
    }

    UGameplayMessageSubsystem* MessageBus = OwningGameInstance->GetSubsystem<UGameplayMessageSubsystem>();
    if (MessageBus == nullptr) {
        return;
    }

    TWeakObjectPtr<UGameNetServiceSubsystem> WeakThis(this);
    MessageBusBridge.Bind(*MessageBus, [WeakThis](const FGameNetOutboundMessage& Message) {
        if (UGameNetServiceSubsystem* Self = WeakThis.Get()) {
            (void)Self->SendRaw(Message.MessageId, Message.Payload, Message.SequenceId);
        }
    });

    RemoteLogListenerHandle = MessageBus->RegisterListener<FGameRemoteLogEntry>(
        TAG_Game_Log_Remote,
        [WeakThis](const FGameRemoteLogEntry& Entry) {
            if (UGameNetServiceSubsystem* Self = WeakThis.Get()) {
                if (Self->bRemoteLogForwardingEnabled && Self->IsConnected()) {
                    (void)Self->SendStruct<FGameRemoteLogEntry>(NET_MSG_REMOTE_LOG, Entry);
                }
            }
        }
    );
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
 * @brief 驱动已连接状态下的周期心跳保活与静默超时判定
 * @param DeltaTimeSeconds 帧间隔秒数
 */
void UGameNetServiceSubsystem::TickHeartbeat(float DeltaTimeSeconds) {
    const ENetHeartbeatAction Action = HeartbeatKeeper.Tick(DeltaTimeSeconds);
    if (Action == ENetHeartbeatAction::SendPing) {
        const uint32 PingSeq = PendingRequestTracker.AllocateSequenceId();
        if (SendRaw(HeartbeatKeeper.GetConfig().PingMessageId, {}, PingSeq).IsSuccess()) {
            HeartbeatKeeper.RecordPingSent(PingSeq, MonotonicClockSeconds);
        }
        return;
    }
    if (Action == ENetHeartbeatAction::TimeoutDisconnect) {
        GAME_LOG(Warning, TEXT("[Service] 心跳静默超时，主动断开并尝试重连 (Endpoint=%s)。"), *TargetEndpoint);
        ActiveTransport->Disconnect(1001, TEXT("HeartbeatTimeout"));
    }
}

/**
 * @brief 驱动断线后的指数退避重连倒计时与重试发起
 * @param DeltaTimeSeconds 帧间隔秒数
 */
void UGameNetServiceSubsystem::TickReconnect(float DeltaTimeSeconds) {
    if (!bInReconnectCycle || !ReconnectPolicy.IsWaitingForRetry() || !ActiveTransport.IsValid()) {
        return;
    }
    if (!ReconnectPolicy.AdvanceTimer(DeltaTimeSeconds)) {
        return;
    }

    GAME_LOG(
        Log,
        TEXT("[Service] 执行第 %d 次指数退避重连: Endpoint=%s"),
        ReconnectPolicy.GetCurrentAttempt(),
        *TargetEndpoint
    );
    const bool bStarted = ActiveTransport->Connect(TargetEndpoint).IsSuccess();
    if (!bStarted && !ReconnectPolicy.IsWaitingForRetry() && !ReconnectPolicy.IsExhausted()) {
        TryEnterReconnectCycle();
    }
}

/**
 * @brief 绑定当前活跃传输驱动的状态、收包与错误委托
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
    TransportErrorHandle = ActiveTransport->OnTransportError().AddUObject(
        this,
        &UGameNetServiceSubsystem::HandleTransportError
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
    if (TransportErrorHandle.IsValid()) {
        ActiveTransport->OnTransportError().Remove(TransportErrorHandle);
        TransportErrorHandle.Reset();
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
        GAME_LOG(Log, TEXT("[Service] 长连接已建立: Endpoint=%s"), *TargetEndpoint);
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
    (void)MessageDispatcher.DispatchPacket(Header, Payload);
    MessageBusBridge.BroadcastInboundPacket(Header, Payload);
}

/**
 * @brief 接收并记录底层传输驱动上报的通道错误原因
 * @param ErrorDetail 底层错误详情
 */
void UGameNetServiceSubsystem::HandleTransportError(const FString& ErrorDetail) {
    GAME_LOG(Warning, TEXT("[Service] 底层传输通道上报异常: %s"), *ErrorDetail);
}

/**
 * @brief 在意外断线后尝试开启下一轮指数退避重连周期
 */
void UGameNetServiceSubsystem::TryEnterReconnectCycle() {
    const TOptional<float> ScheduledSeconds = ReconnectPolicy.ScheduleNextAttempt();
    if (ScheduledSeconds.IsSet()) {
        const bool bWasAlreadyReconnecting = bInReconnectCycle;
        bInReconnectCycle = true;
        GAME_LOG(
            Warning,
            TEXT("[Service] 意外断线，已调度第 %d 次重连，等待 %.2fs: Endpoint=%s"),
            ReconnectPolicy.GetCurrentAttempt(),
            *ScheduledSeconds,
            *TargetEndpoint
        );
        if (!bWasAlreadyReconnecting) {
            MessageBusBridge.BroadcastConnectionState(ENetConnectionState::Reconnecting, TargetEndpoint);
        }
        return;
    }

    GAME_LOG(
        Warning,
        TEXT("[Service] 自动重连配额已耗尽或已禁用，进入最终断开态: Endpoint=%s"),
        *TargetEndpoint
    );
    bInReconnectCycle = false;
    MessageBusBridge.BroadcastConnectionState(ENetConnectionState::Disconnected, TargetEndpoint);
}
