/**
 * @file NetPendingRequestTracker.h
 * @brief 异步 Request-Response 序列号分配、响应关联、单请求取消、超时扫描与断线统一结算器
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/FCancellationToken.h"
#include "Dispatch/NetDispatchTypes.h"

/**
 * @brief 挂起异步网络请求生命周期与超时跟踪表（重入安全、零滞留内存、单次哈希结算）
 */
class GAMENETSERVICE_API FNetPendingRequestTracker final {
private:
    struct FPendingRequestEntry {
        uint32 ExpectedResponseMessageId{0};
        float RemainingSeconds{DEFAULT_REQUEST_TIMEOUT_SECONDS};
        FNetPendingResponseHandler Callback;
        FCancellationTokenRegistration CancelRegistration;
    };

public:
    static constexpr float DEFAULT_REQUEST_TIMEOUT_SECONDS = 10.0f;
    static constexpr float MIN_TIMEOUT_FLOOR_SECONDS = 0.01f;

public:
    FNetPendingRequestTracker() = default;

    /**
     * @brief 分配下一个非零递增请求序列号（自动跳过保留给主动推送的 0 值与当前仍在途的冲突序号）
     * @return 非零请求序列号
     */
    [[nodiscard]] uint32 AllocateSequenceId() noexcept;

    /**
     * @brief 登记一条等待对端回包的异步请求及超时回调
     * @param SequenceId 发包使用的非零序列号
     * @param ExpectedResponseMessageId 期望匹配的响应协议 ID（传 0 表示仅按 SequenceId 匹配）
     * @param TimeoutSeconds 超时秒数上限
     * @param Callback 完成、取消、断线或超时回调闭包
     * @param CancelRegistration 可选关联的取消令牌注册句柄（请求结算后自动解绑）
     */
    void RegisterPendingRequest(
        uint32 SequenceId,
        uint32 ExpectedResponseMessageId,
        float TimeoutSeconds,
        FNetPendingResponseHandler Callback,
        FCancellationTokenRegistration CancelRegistration = {}
    );

    /**
     * @brief 为已登记的在途请求回绑取消令牌注册句柄（避免先注册令牌再入表导致的同步取消竞态）
     * @param SequenceId 在途请求序列号
     * @param CancelRegistration 取消令牌注销控制句柄
     * @return 请求仍在途并成功挂载返回 true；若请求已被同步取消或结算返回 false
     */
    bool AttachCancellationRegistration(uint32 SequenceId, FCancellationTokenRegistration CancelRegistration);

    /**
     * @brief 尝试用到达的下行数据包匹配并结算对应的挂起异步请求（单次哈希定位）
     * @param Header 到达报文头
     * @param Payload 到达负载二进制切片
     * @return 命中并结算一条挂起请求时返回 true
     */
    bool TryCompleteResponse(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload);

    /**
     * @brief 主动取消并移除指定序列号的挂起异步请求（立即以 Cancelled 原因结算回调）
     * @param SequenceId 待取消的请求序列号
     * @return 成功找到并取消该挂起请求返回 true，不存在或已结算返回 false
     */
    bool CancelRequest(uint32 SequenceId);

    /**
     * @brief 推进所有挂起异步请求的超时计时器，到期请求立即触发 TimedOut 回调并清理（重入安全）
     * @param DeltaTimeSeconds 帧间隔秒数
     */
    void TickTimeouts(float DeltaTimeSeconds);

    /**
     * @brief 立即以指定失败原因结算并清空全部挂起请求（断线或子系统销毁时保障零悬挂回调）
     * @param FailureReason 失败原因枚举
     */
    void FailAll(ENetRequestResult FailureReason);

    /**
     * @brief 重置请求序列号计数器与全部缓存（不触发回调，用于全新重置）
     */
    void Reset() noexcept;

    /**
     * @brief 查询当前正在等待响应的挂起异步请求总数
     * @return 挂起请求数量
     */
    [[nodiscard]] int32 GetPendingCount() const noexcept {
        return PendingRequests.Num();
    }

private:
    uint32 NextSequenceCounter{0};
    TMap<uint32, FPendingRequestEntry> PendingRequests;
};
