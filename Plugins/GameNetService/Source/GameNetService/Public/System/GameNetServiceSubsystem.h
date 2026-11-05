/**
 * @file GameNetServiceSubsystem.h
 * @brief 跨地图常驻业务网络门面子系统（轻量级生命周期编排，协调抽象传输驱动、序列化器、路由表、策略器与总线桥接）
 */

#pragma once

#include "CoreMinimal.h"
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

#include "GameNetServiceSubsystem.generated.h"

/**
 * @brief 业务网络长连接常驻门面子系统（仅依赖抽象契约与单一职责子组件，单机与联机共享 100% 一致调用链）
 */
UCLASS()
class GAMENETSERVICE_API UGameNetServiceSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    UGameNetServiceSubsystem() = default;

    /**
     * @brief 子系统初始化：默认装配单机内存回环驱动与 JSON 序列化器，并挂载消息总线桥接器
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
     * @brief 配置指数退避自动重连策略
     * @param InConfig 重连策略配置参数
     * @param RandomSeed 随机抖动种子
     */
    void SetReconnectConfig(const FNetReconnectConfig& InConfig, int32 RandomSeed = 1337);

    /**
     * @brief 配置周期心跳保活与对端失步超时阈值
     * @param InConfig 心跳配置参数
     */
    void SetHeartbeatConfig(const FNetHeartbeatConfig& InConfig);

    /**
     * @brief 发起向目标服务端（或单机回环虚拟地址）的连接
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
     * @brief 查询当前正在进行的自动重连累计尝试次数
     * @return 重连尝试序号
     */
    UFUNCTION(BlueprintPure, Category = "GameNet|Service")
    int32 GetReconnectAttempt() const noexcept {
        return ReconnectPolicy.GetCurrentAttempt();
    }

    /**
     * @brief 发送原始二进制负载数据包
     * @param MessageId 业务协议 ID
     * @param Payload 负载二进制切片
     * @param SequenceId 序列号（默认 0）
     * @return 写入发送通道成功返回 true
     */
    bool SendRaw(uint32 MessageId, TConstArrayView<uint8> Payload, uint32 SequenceId = 0);

    /**
     * @brief 发送带超时与序列号自动关联的异步原始 Request-Response 请求
     * @param RequestMessageId 请求协议 ID
     * @param ExpectedResponseMessageId 期望匹配的响应协议 ID（传 0 仅按序列号匹配）
     * @param Payload 请求负载切片
     * @param TimeoutSeconds 超时秒数
     * @param Callback 请求完成、断线或超时回调
     * @return 分配的非零请求 SequenceId；发送失败时立即回调 SendFailed 并返回 0
     */
    uint32 SendRawRequest(
        uint32 RequestMessageId,
        uint32 ExpectedResponseMessageId,
        TConstArrayView<uint8> Payload,
        float TimeoutSeconds,
        FNetPendingResponseHandler Callback
    );

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
     * @return 成功移除返回 true
     */
    bool UnregisterHandler(uint32 MessageId, int64 HandlerId);

    /**
     * @brief 驱动网络队列派发、请求超时扫描、心跳探针与退避重连状态机
     * @param DeltaTimeSeconds 步进秒数
     */
    void Tick(float DeltaTimeSeconds);

    /**
     * @brief 泛型便捷发送：使用当前活跃序列化器将强类型 USTRUCT 编码后发送
     * @tparam TStruct 反射结构体类型
     * @param MessageId 业务协议 ID
     * @param StructPayload 结构体实例
     * @return 序列化且发送成功返回 true
     */
    template <typename TStruct>
    bool SendStruct(uint32 MessageId, const TStruct& StructPayload);

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
     * @brief 泛型便捷异步 RPC：自动编码请求结构体并在收到响应时自动反解响应结构体
     * @tparam TReqStruct 请求反射结构体类型
     * @tparam TRspStruct 响应反射结构体类型
     * @param RequestMessageId 请求协议 ID
     * @param ExpectedResponseMessageId 期望响应协议 ID
     * @param RequestPayload 请求结构体实例
     * @param TimeoutSeconds 超时秒数上限
     * @param Callback 结果回调（Success 时保证指针非空，失败/超时时指针为空）
     * @return 分配的非零请求 SequenceId；失败返回 0
     */
    template <typename TReqStruct, typename TRspStruct>
    uint32 SendStructRequest(
        uint32 RequestMessageId,
        uint32 ExpectedResponseMessageId,
        const TReqStruct& RequestPayload,
        float TimeoutSeconds,
        TFunction<void(ENetRequestResult, const TRspStruct*)> Callback
    );

public:
    /**
     * @brief 安全获取业务网络门面子系统指针（可空）
     * @param WorldContextObject 世界上下文对象
     * @return 子系统实例指针，未就绪时返回 nullptr
     */
    [[nodiscard]] static UGameNetServiceSubsystem* GetPtr(const UObject* WorldContextObject);

    /**
     * @brief 获取业务网络门面子系统引用（断言有效）
     * @param WorldContextObject 世界上下文对象
     * @return 子系统引用
     */
    [[nodiscard]] static UGameNetServiceSubsystem& Get(const UObject* WorldContextObject);

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

private:
    /**
     * @brief CoreTicker 帧回调桥接入口
     * @param DeltaTimeSeconds 帧间隔秒数
     * @return 始终返回 true 以保持常驻周期调度
     */
    bool HandleCoreTicker(float DeltaTimeSeconds);

    /**
     * @brief 绑定当前活跃传输驱动的状态与收包委托
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
    bool bManualDisconnect{true};
    bool bInReconnectCycle{false};
    bool bRemoteLogForwardingEnabled{true};
    FDelegateHandle TransportStateHandle;
    FDelegateHandle TransportPacketHandle;
    FTSTicker::FDelegateHandle CoreTickerHandle;
};

template <typename TStruct>
bool UGameNetServiceSubsystem::SendStruct(uint32 MessageId, const TStruct& StructPayload) {
    if (!ActiveSerializer.IsValid()) {
        return false;
    }

    const TOptional<TArray<uint8>> Encoded = ActiveSerializer->SerializeStruct(StructPayload);
    if (!Encoded.IsSet()) {
        return false;
    }

    return SendRaw(MessageId, *Encoded, 0);
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
            const TOptional<TStruct> Decoded = Self->ActiveSerializer->DeserializeStruct<TStruct>(Payload);
            if (Decoded.IsSet()) {
                Callback(Header, *Decoded);
            }
        }
    );
}

template <typename TReqStruct, typename TRspStruct>
uint32 UGameNetServiceSubsystem::SendStructRequest(
    uint32 RequestMessageId,
    uint32 ExpectedResponseMessageId,
    const TReqStruct& RequestPayload,
    float TimeoutSeconds,
    TFunction<void(ENetRequestResult, const TRspStruct*)> Callback
) {
    if (!Callback) {
        return 0;
    }
    if (!ActiveSerializer.IsValid()) {
        Callback(ENetRequestResult::SendFailed, nullptr);
        return 0;
    }

    const TOptional<TArray<uint8>> EncodedReq = ActiveSerializer->SerializeStruct(RequestPayload);
    if (!EncodedReq.IsSet()) {
        Callback(ENetRequestResult::SendFailed, nullptr);
        return 0;
    }

    TWeakObjectPtr<UGameNetServiceSubsystem> WeakThis(this);
    return SendRawRequest(
        RequestMessageId,
        ExpectedResponseMessageId,
        *EncodedReq,
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
            const TOptional<TRspStruct> DecodedRsp = Self->ActiveSerializer->DeserializeStruct<TRspStruct>(RspPayload);
            if (!DecodedRsp.IsSet()) {
                Callback(ENetRequestResult::DeserializeFailed, nullptr);
                return;
            }
            Callback(ENetRequestResult::Success, &(*DecodedRsp));
        }
    );
}

