/**
 * @file GameNetServiceSubsystem.h
 * @brief 跨地图常驻业务网络门面子系统（轻量级生命周期编排，协调抽象传输驱动、序列化器、路由表、策略器与总线桥接）
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/FCancellationToken.h"
#include "Async/TNetRequestStructAwaiter.h"
#include "Bridge/NetMessageBusBridge.h"
#include "Containers/Ticker.h"
#include "Dispatch/NetDispatchTypes.h"
#include "Dispatch/NetMessageDispatcher.h"
#include "Dispatch/NetPendingRequestTracker.h"
#include "Policy/NetHeartbeatKeeper.h"
#include "Policy/NetReconnectPolicy.h"
#include "Serializer/INetSerializer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Transport/INetTransport.h"
#include "Types/TGameResult.h"

#include "GameNetServiceSubsystem.generated.h"

/**
 * @brief 业务网络长连接常驻门面子系统（仅依赖抽象契约与单一职责子组件，单机与联机共享 100% 一致调用链）
 */
UCLASS()
class GAMENETSERVICE_API UGameNetServiceSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

private:
    struct FScopedSendBuffer final {
        /**
         * @brief 优先租用成员复用缓冲；发生重入发送时自动回退栈局部缓冲确保零数据覆写
         * @param InOwner 所属子系统实例引用
         */
        explicit FScopedSendBuffer(UGameNetServiceSubsystem& InOwner) noexcept
            : Owner(InOwner)
            , bUsingSharedScratch(!InOwner.bSendScratchInUse) {
            if (bUsingSharedScratch) {
                Owner.bSendScratchInUse = true;
            }
        }

        /**
         * @brief 析构时归还成员复用缓冲占用标记
         */
        ~FScopedSendBuffer() noexcept {
            if (bUsingSharedScratch) {
                Owner.bSendScratchInUse = false;
            }
        }

        FScopedSendBuffer(const FScopedSendBuffer&) = delete;
        FScopedSendBuffer& operator=(const FScopedSendBuffer&) = delete;

        /**
         * @brief 获取当前安全可写的目标字节数组引用
         * @return 目标字节数组引用
         */
        [[nodiscard]] TArray<uint8>& GetBuffer() noexcept {
            return bUsingSharedScratch ? Owner.SendScratchBuffer : ReentrantFallbackBuffer;
        }

    private:
        UGameNetServiceSubsystem& Owner;
        TArray<uint8> ReentrantFallbackBuffer;
        bool bUsingSharedScratch{false};
    };

public:
    UGameNetServiceSubsystem() = default;

    /**
     * @brief 安全获取业务网络门面子系统指针（支持 UWorld、UGameInstance、UGameInstanceSubsystem 与场景对象）
     * @param WorldContextObject 世界或子系统上下文对象
     * @return 子系统实例指针，未就绪时返回 nullptr
     */
    UFUNCTION(BlueprintPure, Category = "GameNet|Service", meta = (WorldContext = "WorldContextObject"))
    static UGameNetServiceSubsystem* GetPtr(const UObject* WorldContextObject);

    /**
     * @brief 获取业务网络门面子系统引用（断言有效）
     * @param WorldContextObject 世界或子系统上下文对象
     * @return 子系统引用
     */
    [[nodiscard]] static UGameNetServiceSubsystem& Get(const UObject* WorldContextObject);

    /**
     * @brief 子系统初始化：默认装配单机内存回环驱动与 JSON 序列化器，并挂载消息总线桥接器与远程日志监听
     * @param Collection 子系统依赖集合
     */
    void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * @brief 子系统销毁：注销轮询、解绑消息总线、断开连接并安全结算全部挂起请求
     */
    void Deinitialize() override;

    /**
     * @brief 注入或热切换底层抽象传输驱动实例（切换前自动安全断开旧驱动并结算未决请求）
     * @param NewTransport 新传输驱动共享指针
     */
    void SetTransport(TSharedPtr<INetTransport> NewTransport);

    /**
     * @brief 获取当前挂载的抽象传输驱动实例
     * @return 传输驱动共享指针
     */
    [[nodiscard]] TSharedPtr<INetTransport> GetTransport() const noexcept {
        return ActiveTransport;
    }

    /**
     * @brief 注入或热切换网络负载序列化适配器（如 JSON、UE 二进制 Archive 或外部 Protobuf 适配器）
     * @param NewSerializer 新序列化器共享指针
     */
    void SetSerializer(TSharedPtr<INetSerializer> NewSerializer);

    /**
     * @brief 获取当前生效的序列化适配器
     * @return 序列化器共享指针
     */
    [[nodiscard]] TSharedPtr<INetSerializer> GetSerializer() const noexcept {
        return ActiveSerializer;
    }

    /**
     * @brief 使用运行期高熵随机种子配置指数退避自动重连策略（生产环境防同步重连雪崩）
     * @param InConfig 重连策略配置参数
     */
    void SetReconnectConfig(const FNetReconnectConfig& InConfig);

    /**
     * @brief 使用指定确定性随机种子配置指数退避自动重连策略（专供自动化测试精确复现）
     * @param InConfig 重连策略配置参数
     * @param DeterministicSeed 确定性随机种子
     */
    void SetReconnectConfig(const FNetReconnectConfig& InConfig, uint64 DeterministicSeed);

    /**
     * @brief 配置周期心跳保活与对端失步超时阈值
     * @param InConfig 心跳配置参数
     */
    void SetHeartbeatConfig(const FNetHeartbeatConfig& InConfig);

    /**
     * @brief 发起向目标服务端（或单机回环虚拟地址）的连接（已连接或连接中时安全拒绝且不破坏现有会话状态）
     * @param Endpoint 目标连接地址
     * @return 成功触发连接流程返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameNet|Service")
    bool Connect(const FString& Endpoint);

    /**
     * @brief 主动断开当前连接并停止自动重连循环
     * @param ReasonCode 关闭状态码
     * @param ReasonDetail 关闭原因描述
     */
    UFUNCTION(BlueprintCallable, Category = "GameNet|Service")
    void Disconnect(int32 ReasonCode = 1000, const FString& ReasonDetail = TEXT("ClientManualDisconnect"));

    /**
     * @brief 查询当前对外综合连接状态（退避等待重连期间返回 Reconnecting）
     * @return 连接状态枚举
     */
    UFUNCTION(BlueprintPure, Category = "GameNet|Service")
    ENetConnectionState GetConnectionState() const noexcept;

    /**
     * @brief 查询当前是否处于已连接可发包状态
     * @return 已连接返回 true
     */
    UFUNCTION(BlueprintPure, Category = "GameNet|Service")
    bool IsConnected() const noexcept;

    /**
     * @brief 查询最近一次心跳采样测得的往返延迟毫秒数
     * @return 往返延迟（毫秒）
     */
    UFUNCTION(BlueprintPure, Category = "GameNet|Service")
    float GetLastRttMilliseconds() const noexcept {
        return HeartbeatKeeper.GetLastRttMilliseconds();
    }

    /**
     * @brief 查询滑动窗口平滑后的平均往返延迟毫秒数
     * @return 平滑平均往返延迟（毫秒）
     */
    UFUNCTION(BlueprintPure, Category = "GameNet|Service")
    float GetAverageRttMilliseconds() const noexcept {
        return HeartbeatKeeper.GetAverageRttMilliseconds();
    }

    /**
     * @brief 查询当前正在进行的自动重连累计尝试次数
     * @return 重连尝试序号
     */
    UFUNCTION(BlueprintPure, Category = "GameNet|Service")
    int32 GetReconnectAttempt() const noexcept {
        return ReconnectPolicy.GetCurrentAttempt();
    }

    /**
     * @brief 开启或关闭对总线远程日志（TAG_Game_Log_Remote）的自动网络转送
     * @param bEnabled 是否开启转送
     */
    UFUNCTION(BlueprintCallable, Category = "GameNet|Service")
    void SetRemoteLogForwardingEnabled(bool bEnabled);

    /**
     * @brief 查询当前是否开启了对总线远程日志的自动网络转送
     * @return 开启返回 true
     */
    UFUNCTION(BlueprintPure, Category = "GameNet|Service")
    bool IsRemoteLogForwardingEnabled() const noexcept;

    /**
     * @brief 发送原始二进制负载数据包
     * @param MessageId 业务协议 ID
     * @param Payload 负载二进制切片
     * @param SequenceId 序列号（默认 0）
     * @return 写入发送通道成功返回 Success，未连接或驱动失败返回带 Game::ModuleId::Net 的 Failure
     */
    TGameStatusResult<void> SendRaw(uint32 MessageId, TConstArrayView<uint8> Payload, uint32 SequenceId = 0);

    /**
     * @brief 发送带超时、序列号自动关联与可选协同取消令牌的异步原始 Request-Response 请求
     * @param RequestMessageId 请求协议 ID
     * @param ExpectedResponseMessageId 期望匹配的响应协议 ID（传 0 仅按序列号匹配）
     * @param Payload 请求负载切片
     * @param TimeoutSeconds 超时秒数
     * @param Callback 请求完成、取消、断线或超时回调
     * @param CancellationToken 可选协同取消令牌（触发取消时自动移除挂起项并回调 Cancelled）
     * @return 分配的非零请求 SequenceId；发送失败或已取消时立即回调对应状态并返回 Failure
     */
    TGameStatusResult<uint32> SendRawRequest(
        uint32 RequestMessageId,
        uint32 ExpectedResponseMessageId,
        TConstArrayView<uint8> Payload,
        float TimeoutSeconds,
        FNetPendingResponseHandler Callback,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 主动取消指定序列号的在途异步请求并立即触发 Cancelled 回调
     * @param SequenceId 请求序列号
     * @return 成功命中并取消返回 true
     */
    bool CancelPendingRequest(uint32 SequenceId);

    /**
     * @brief 注册指定协议 ID 的原始二进制下行消息处理器
     * @param MessageId 监听的协议 ID
     * @param Handler 到达处理闭包
     * @return 处理器唯一注销句柄 ID
     */
    [[nodiscard]] int64 RegisterRawHandler(uint32 MessageId, FNetRawMessageHandler Handler);

    /**
     * @brief 注销指定协议 ID 的消息处理器
     * @param MessageId 协议 ID
     * @param HandlerId 注册时返回的句柄 ID
     * @return 成功找到并移除返回 true，未找到返回 false
     */
    bool UnregisterHandler(uint32 MessageId, int64 HandlerId);

    /**
     * @brief 驱动网络队列派发、请求超时扫描、心跳探针与退避重连状态机
     * @param DeltaTimeSeconds 步进秒数
     */
    void Tick(float DeltaTimeSeconds);

    /**
     * @brief 泛型便捷发送：使用当前活跃序列化器将强类型 USTRUCT 编码后发送（重入安全）
     * @tparam TStruct 反射结构体类型
     * @param MessageId 业务协议 ID
     * @param StructPayload 结构体实例
     * @return 序列化且发送成功返回 Success，失败返回对应 Failure
     */
    template <typename TStruct>
    TGameStatusResult<void> SendStruct(uint32 MessageId, const TStruct& StructPayload);

    /**
     * @brief 泛型便捷订阅：自动使用当前活跃序列化器反解下行负载并触发强类型回调
     * @tparam TStruct 期望接收的反射结构体类型
     * @param MessageId 监听的下行协议 ID
     * @param Callback 强类型结构体回调闭包
     * @return 处理器唯一注销句柄 ID
     */
    template <typename TStruct>
    [[nodiscard]] int64 RegisterStructHandler(
        uint32 MessageId,
        TFunction<void(const FNetPacketHeader&, const TStruct&)> Callback
    );

    /**
     * @brief 泛型便捷异步 RPC：自动编码请求结构体并在收到响应时自动反解响应结构体（重入安全，支持协同取消）
     * @tparam TReqStruct 请求反射结构体类型
     * @tparam TRspStruct 响应反射结构体类型
     * @param RequestMessageId 请求协议 ID
     * @param ExpectedResponseMessageId 期望响应协议 ID
     * @param RequestPayload 请求结构体实例
     * @param TimeoutSeconds 超时秒数上限
     * @param Callback 结果回调（Success 时保证指针非空，失败/超时/取消时指针为空）
     * @param CancellationToken 可选协同取消令牌
     * @return 成功返回分配的 SequenceId，失败返回 Failure
     */
    template <typename TReqStruct, typename TRspStruct>
    TGameStatusResult<uint32> SendStructRequest(
        uint32 RequestMessageId,
        uint32 ExpectedResponseMessageId,
        const TReqStruct& RequestPayload,
        float TimeoutSeconds,
        TFunction<void(ENetRequestResult, const TRspStruct*)> Callback,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 泛型便捷异步 RPC（C++20 协程门面，零拷贝移动交付响应结构体，支持生命周期守卫与协同取消）
     * @tparam TReqStruct 请求反射结构体类型
     * @tparam TRspStruct 响应反射结构体类型
     * @param RequestMessageId 请求协议 ID
     * @param ExpectedResponseMessageId 期望响应协议 ID
     * @param RequestPayload 请求结构体实例
     * @param TimeoutSeconds 超时秒数上限
     * @param CancellationToken 可选协同取消令牌
     * @return 协程挂起器，恢复后交付包含响应结构体或网络错误枚举的 TGameResult
     */
    template <typename TReqStruct, typename TRspStruct>
    TNetRequestStructAwaiter<TReqStruct, TRspStruct> RequestStructAsync(
        uint32 RequestMessageId,
        uint32 ExpectedResponseMessageId,
        const TReqStruct& RequestPayload,
        float TimeoutSeconds = 5.0f,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

private:
    /**
     * @brief 绑定消息总线桥接器与远程日志监听回调
     */
    void BindMessageBusListeners();

    /**
     * @brief CoreTicker 帧回调桥接入口
     * @param DeltaTimeSeconds 帧间隔秒数
     * @return 始终返回 true 以保持常驻周期调度
     */
    bool HandleCoreTicker(float DeltaTimeSeconds);

    /**
     * @brief 驱动已连接状态下的周期心跳保活与静默超时判定
     * @param DeltaTimeSeconds 帧间隔秒数
     */
    void TickHeartbeat(float DeltaTimeSeconds);

    /**
     * @brief 驱动断线后的指数退避重连倒计时与重试发起
     * @param DeltaTimeSeconds 帧间隔秒数
     */
    void TickReconnect(float DeltaTimeSeconds);

    /**
     * @brief 绑定当前活跃传输驱动的状态、收包与错误委托
     */
    void BindActiveTransportEvents();

    /**
     * @brief 解绑当前活跃传输驱动的全部委托
     */
    void UnbindActiveTransportEvents();

    /**
     * @brief 处理底层传输驱动状态变更，联动异步请求清理、重连调度与消息总线桥接广播
     * @param NewTransportState 底层驱动最新状态
     */
    void HandleTransportStateChanged(ENetConnectionState NewTransportState);

    /**
     * @brief 处理底层传输驱动收到的完整数据包，更新心跳、匹配异步响应、多播路由并转发至消息总线
     * @param Header 报文头
     * @param Payload 负载切片
     */
    void HandleTransportPacketReceived(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload);

    /**
     * @brief 接收并记录底层传输驱动上报的通道错误原因
     * @param ErrorDetail 底层错误详情
     */
    void HandleTransportError(const FString& ErrorDetail);

    /**
     * @brief 在意外断线后尝试开启下一轮指数退避重连周期
     */
    void TryEnterReconnectCycle();

private:
    TSharedPtr<INetTransport> ActiveTransport;
    TSharedPtr<INetSerializer> ActiveSerializer;
    FNetMessageDispatcher MessageDispatcher;
    FNetPendingRequestTracker PendingRequestTracker;
    FNetReconnectPolicy ReconnectPolicy;
    FNetHeartbeatKeeper HeartbeatKeeper;
    FNetMessageBusBridge MessageBusBridge;
    FGameplayMessageListenerHandle RemoteLogListenerHandle;
    FString TargetEndpoint;
    double MonotonicClockSeconds{0.0};
    TArray<uint8> SendScratchBuffer;
    bool bSendScratchInUse{false};
    bool bManualDisconnect{true};
    bool bInReconnectCycle{false};
    bool bRemoteLogForwardingEnabled{true};
    FDelegateHandle TransportStateHandle;
    FDelegateHandle TransportPacketHandle;
    FDelegateHandle TransportErrorHandle;
    FTSTicker::FDelegateHandle CoreTickerHandle;
};

template <typename TStruct>
TGameStatusResult<void> UGameNetServiceSubsystem::SendStruct(uint32 MessageId, const TStruct& StructPayload) {
    if (!ActiveSerializer.IsValid()) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::InvalidSerializer,
            TEXT("Serializer is invalid")
        );
    }

    FScopedSendBuffer BufferGuard(*this);
    TArray<uint8>& PayloadBuffer = BufferGuard.GetBuffer();
    const TGameStatusResult<void> EncodeStatus = ActiveSerializer->SerializeStructTo(StructPayload, PayloadBuffer);
    if (EncodeStatus.IsFailure()) {
        return TGameStatusResult<void>::Failure(EncodeStatus.GetError());
    }

    return SendRaw(MessageId, PayloadBuffer, 0);
}

template <typename TStruct>
int64 UGameNetServiceSubsystem::RegisterStructHandler(
    uint32 MessageId,
    TFunction<void(const FNetPacketHeader&, const TStruct&)> Callback
) {
    if (!Callback) {
        return 0;
    }

    TWeakObjectPtr<UGameNetServiceSubsystem> WeakThis(this);
    return RegisterRawHandler(
        MessageId,
        [WeakThis, Callback = MoveTemp(Callback)](const FNetPacketHeader& Header, TConstArrayView<uint8> Payload) {
            const UGameNetServiceSubsystem* Self = WeakThis.Get();
            if (Self == nullptr || !Self->ActiveSerializer.IsValid()) {
                return;
            }
            TStruct Decoded;
            const TGameStatusResult<void> DecodeResult = Self->ActiveSerializer->DeserializeStructTo(Payload, Decoded);
            if (DecodeResult.IsSuccess()) {
                Callback(Header, Decoded);
            }
        }
    );
}

template <typename TReqStruct, typename TRspStruct>
TGameStatusResult<uint32> UGameNetServiceSubsystem::SendStructRequest(
    uint32 RequestMessageId,
    uint32 ExpectedResponseMessageId,
    const TReqStruct& RequestPayload,
    float TimeoutSeconds,
    TFunction<void(ENetRequestResult, const TRspStruct*)> Callback,
    FCancellationToken CancellationToken
) {
    if (!Callback) {
        return TGameStatusResult<uint32>::Failure(
            Game::ModuleId::Net,
            Game::CommonErrorCodes::InvalidParameter,
            TEXT("Invalid callback")
        );
    }
    if (CancellationToken.IsCancellationRequested()) {
        Callback(ENetRequestResult::Cancelled, nullptr);
        return TGameStatusResult<uint32>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::RequestCancelled,
            TEXT("Request cancelled before dispatch")
        );
    }
    if (!ActiveSerializer.IsValid()) {
        Callback(ENetRequestResult::SendFailed, nullptr);
        return TGameStatusResult<uint32>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::InvalidSerializer,
            TEXT("Serializer is invalid")
        );
    }

    FScopedSendBuffer BufferGuard(*this);
    TArray<uint8>& PayloadBuffer = BufferGuard.GetBuffer();
    const TGameStatusResult<void> EncodeStatus = ActiveSerializer->SerializeStructTo(RequestPayload, PayloadBuffer);
    if (EncodeStatus.IsFailure()) {
        Callback(ENetRequestResult::SendFailed, nullptr);
        return TGameStatusResult<uint32>::Failure(EncodeStatus.GetError());
    }

    TWeakObjectPtr<UGameNetServiceSubsystem> WeakThis(this);
    return SendRawRequest(
        RequestMessageId,
        ExpectedResponseMessageId,
        PayloadBuffer,
        TimeoutSeconds,
        [WeakThis, Callback = MoveTemp(Callback)](
            ENetRequestResult Result,
            const FNetPacketHeader&,
            TConstArrayView<uint8> RspPayload
        ) {
            if (Result != ENetRequestResult::Success) {
                Callback(Result, nullptr);
                return;
            }
            const UGameNetServiceSubsystem* Self = WeakThis.Get();
            if (Self == nullptr || !Self->ActiveSerializer.IsValid()) {
                Callback(ENetRequestResult::ConnectionClosed, nullptr);
                return;
            }
            TRspStruct DecodedRsp;
            const TGameStatusResult<void> DecodeResult = Self->ActiveSerializer->DeserializeStructTo(RspPayload, DecodedRsp);
            if (DecodeResult.IsFailure()) {
                Callback(ENetRequestResult::DeserializeFailed, nullptr);
                return;
            }
            Callback(ENetRequestResult::Success, &DecodedRsp);
        },
        MoveTemp(CancellationToken)
    );
}

template <typename TReqStruct, typename TRspStruct>
TNetRequestStructAwaiter<TReqStruct, TRspStruct> UGameNetServiceSubsystem::RequestStructAsync(
    uint32 RequestMessageId,
    uint32 ExpectedResponseMessageId,
    const TReqStruct& RequestPayload,
    float TimeoutSeconds,
    FCancellationToken CancellationToken
) {
    using FAwaiter = TNetRequestStructAwaiter<TReqStruct, TRspStruct>;
    using FResultType = typename FAwaiter::FResultType;

    TWeakObjectPtr<UGameNetServiceSubsystem> WeakThis(this);
    return FAwaiter(
        [WeakThis, RequestMessageId, ExpectedResponseMessageId, Payload = RequestPayload, TimeoutSeconds, Token = MoveTemp(CancellationToken)](
            typename FAwaiter::FCompleteCallback OnCompleted
        ) mutable {
            UGameNetServiceSubsystem* Self = WeakThis.Get();
            if (Self == nullptr) {
                OnCompleted(FResultType::Failure(ENetRequestResult::ConnectionClosed));
                return;
            }
            if (Token.IsCancellationRequested()) {
                OnCompleted(FResultType::Failure(ENetRequestResult::Cancelled));
                return;
            }
            if (!Self->ActiveSerializer.IsValid()) {
                OnCompleted(FResultType::Failure(ENetRequestResult::SendFailed));
                return;
            }

            FScopedSendBuffer BufferGuard(*Self);
            TArray<uint8>& EncodedBytes = BufferGuard.GetBuffer();
            if (Self->ActiveSerializer->SerializeStructTo(Payload, EncodedBytes).IsFailure()) {
                OnCompleted(FResultType::Failure(ENetRequestResult::SendFailed));
                return;
            }

            (void)Self->SendRawRequest(
                RequestMessageId,
                ExpectedResponseMessageId,
                EncodedBytes,
                TimeoutSeconds,
                [WeakThis, Complete = MoveTemp(OnCompleted)](
                    ENetRequestResult ReqResult,
                    const FNetPacketHeader&,
                    TConstArrayView<uint8> RspBytes
                ) {
                    if (ReqResult != ENetRequestResult::Success) {
                        Complete(FResultType::Failure(ReqResult));
                        return;
                    }
                    const UGameNetServiceSubsystem* LiveSelf = WeakThis.Get();
                    if (LiveSelf == nullptr || !LiveSelf->ActiveSerializer.IsValid()) {
                        Complete(FResultType::Failure(ENetRequestResult::ConnectionClosed));
                        return;
                    }
                    TRspStruct DecodedRsp;
                    if (LiveSelf->ActiveSerializer->DeserializeStructTo(RspBytes, DecodedRsp).IsFailure()) {
                        Complete(FResultType::Failure(ENetRequestResult::DeserializeFailed));
                        return;
                    }
                    Complete(FResultType::Success(MoveTemp(DecodedRsp)));
                },
                MoveTemp(Token)
            );
        }
    );
}
