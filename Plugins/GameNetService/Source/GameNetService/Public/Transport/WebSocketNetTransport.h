/**
 * @file WebSocketNetTransport.h
 * @brief 基于 UE 原生 WebSockets 模块与无锁 MPSC 队列跨线程安全派发的长连接传输驱动
 */

#pragma once

#include "CoreMinimal.h"
#include "Codec/NetPacketRingBuffer.h"
#include "Containers/Queue.h"
#include "Transport/INetTransport.h"
#include "Transport/WebSocketFragmentAssembler.h"
#include "Transport/WebSocketTransportTypes.h"

class IWebSocket;

/**
 * @brief WebSocket (WS / WSS) 二进制帧长连接传输驱动（后台事件入无锁队列，游戏主线程统一派发）
 */
class GAMENETSERVICE_API FWebSocketNetTransport final : public INetTransport {
private:
    enum class EInboundEventKind : uint8 {
        Connected = 0,
        ConnectionError,
        Closed,
        BinaryFragment
    };

    struct FInboundSocketEvent {
        uint64 Generation{0};
        EInboundEventKind Kind{EInboundEventKind::Connected};
        int32 StatusCode{0};
        bool bWasClean{false};
        bool bIsLastFragment{true};
        FString TextDetail;
        TArray<uint8> BinaryData;
    };

    using FThreadSafeEventQueue = TQueue<FInboundSocketEvent, EQueueMode::Mpsc>;

public:
    /**
     * @brief 构造 WebSocket 传输驱动
     * @param InMaxPayloadBytes 单帧允许接收的最大业务负载字节数
     * @param InCustomFactory 可选底层 WebSocket 实例工厂（缺省使用引擎 FWebSocketsModule，单测可注入桩件）
     */
    explicit FWebSocketNetTransport(
        uint32 InMaxPayloadBytes = FNetPacketHeader::DEFAULT_MAX_PAYLOAD_BYTES,
        FWebSocketFactory InCustomFactory = nullptr
    );

    /**
     * @brief 析构时安全解绑底层回调并关闭物理连接
     */
    ~FWebSocketNetTransport() override;

    FWebSocketNetTransport(const FWebSocketNetTransport&) = delete;
    FWebSocketNetTransport& operator=(const FWebSocketNetTransport&) = delete;
    FWebSocketNetTransport(FWebSocketNetTransport&&) = delete;
    FWebSocketNetTransport& operator=(FWebSocketNetTransport&&) = delete;

    /**
     * @brief 发起向指定 ws:// 或 wss:// 端点的异步握手连接
     * @param Endpoint WebSocket 目标 URL
     * @return 成功创建底层实例并发起连接返回 true
     */
    bool Connect(const FString& Endpoint) override;

    /**
     * @brief 主动关闭当前 WebSocket 连接并使旧代际回调失效
     * @param ReasonCode 标准 WebSocket 关闭状态码（如 1000）
     * @param ReasonDetail 关闭原因描述
     */
    void Disconnect(int32 ReasonCode, const FString& ReasonDetail) override;

    /**
     * @brief 将报文头与负载编码为 16 字节线格式二进制帧并通过 WebSocket Binary 通道发送
     * @param Header 报文头
     * @param Payload 负载二进制切片
     * @return 已连接且帧合法时成功写入返回 true
     */
    bool Send(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload) override;

    /**
     * @brief 查询当前连接状态
     * @return 当前连接状态枚举
     */
    [[nodiscard]] ENetConnectionState GetState() const noexcept override {
        return CurrentState;
    }

    /**
     * @brief 在游戏主线程消费无锁队列中的后台 Socket 事件并触发上层委托
     * @param DeltaTimeSeconds 帧间隔秒数
     */
    void Tick(float DeltaTimeSeconds) override;

    /**
     * @brief 获取连接状态变更广播委托
     * @return 状态变更委托引用
     */
    FOnNetConnectionStateChanged& OnStateChanged() override {
        return StateChangedEvent;
    }

    /**
     * @brief 获取完整数据包到达广播委托
     * @return 数据包到达委托引用
     */
    FOnNetPacketReceived& OnPacketReceived() override {
        return PacketReceivedEvent;
    }

    /**
     * @brief 获取底层通道异常广播委托
     * @return 错误委托引用
     */
    FOnNetTransportError& OnTransportError() override {
        return TransportErrorEvent;
    }

    /**
     * @brief 设置握手阶段协商的 WebSocket 子协议名
     * @param InSubProtocol 子协议字符串
     */
    void SetSubProtocol(const FString& InSubProtocol) {
        SubProtocol = InSubProtocol;
    }

    /**
     * @brief 设置 HTTP Upgrade 握手请求扩展头
     * @param InUpgradeHeaders 键值对映射表
     */
    void SetUpgradeHeaders(const TMap<FString, FString>& InUpgradeHeaders) {
        UpgradeHeaders = InUpgradeHeaders;
    }

private:
    /**
     * @brief 挂载底层 IWebSocket 事件回调至无锁跨线程队列（按代际隔离）
     * @param TargetSocket 待绑定的底层 Socket 引用
     * @param BoundGeneration 当前连接会话代际号
     */
    void BindSocketCallbacks(const TSharedRef<IWebSocket>& TargetSocket, uint64 BoundGeneration);

    /**
     * @brief 解绑并安全释放当前持有的底层 IWebSocket 实例
     * @param CloseCode 关闭码
     * @param CloseReason 关闭描述
     * @param bShouldInvokeClose 是否需要向未关闭的底层实例发起 Close 调用
     */
    void ReleaseSocket(int32 CloseCode, const FString& CloseReason, bool bShouldInvokeClose);

    /**
     * @brief 处理单条后台队列出队的二进制分片并在完整消息收齐后送入环形帧解析器
     * @param FragmentBytes 分片数据
     * @param bIsLastFragment 是否为当前 WebSocket 消息的最后一个分片
     */
    void ProcessBinaryFragment(TConstArrayView<uint8> FragmentBytes, bool bIsLastFragment);

    /**
     * @brief 从环形解析器连续弹出所有完整帧并广播；遇协议违例立即熔断断连
     */
    void DrainDecodedFrames();

    /**
     * @brief 因对端发来畸形头或超限包触发协议级熔断保护
     * @param ErrorReason 熔断原因说明
     */
    void TriggerProtocolCircuitBreaker(const FString& ErrorReason);

    /**
     * @brief 更新内部连接状态并在状态变化时触发广播
     * @param NewState 目标新状态
     */
    void TransitionState(ENetConnectionState NewState);

private:
    FWebSocketFactory SocketFactory;
    TSharedRef<FThreadSafeEventQueue, ESPMode::ThreadSafe> SharedEventQueue;
    TSharedPtr<IWebSocket> ActiveSocket;
    FWebSocketFragmentAssembler FragmentAssembler;
    FNetPacketRingBuffer ReceiveRingBuffer;
    FString SubProtocol;
    TMap<FString, FString> UpgradeHeaders;
    uint64 ActiveGeneration{0};
    ENetConnectionState CurrentState{ENetConnectionState::Disconnected};
    FOnNetConnectionStateChanged StateChangedEvent;
    FOnNetPacketReceived PacketReceivedEvent;
    FOnNetTransportError TransportErrorEvent;
};

