/**
 * @file TNetRequestStructAwaiter.h
 * @brief 业务网络 Request-Response 异步结构体请求 C++20 协程挂起器
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/TGameCoroTask.h"
#include "Dispatch/NetDispatchTypes.h"
#include "Types/TGameResult.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UGameNetServiceSubsystem;

/**
 * @brief 挂起协程并等待网络响应的泛型 Awaiter（支持生命周期守卫熔断）
 * @tparam TReqStruct 请求反射结构体类型
 * @tparam TRspStruct 响应反射结构体类型
 */
template <typename TReqStruct, typename TRspStruct>
class TNetRequestStructAwaiter {
public:
    /**
     * @brief 构造网络结构体请求等待器
     * @param InSubsystem 网络服务门面子系统指针
     * @param InRequestMessageId 请求协议 ID
     * @param InExpectedResponseMessageId 期望响应协议 ID
     * @param InPayload 请求结构体载荷
     * @param InTimeoutSeconds 超时秒数
     */
    TNetRequestStructAwaiter(
        UGameNetServiceSubsystem* InSubsystem,
        uint32 InRequestMessageId,
        uint32 InExpectedResponseMessageId,
        TReqStruct InPayload,
        float InTimeoutSeconds
    )
        : SubsystemWeak(InSubsystem)
        , RequestMessageId(InRequestMessageId)
        , ExpectedResponseMessageId(InExpectedResponseMessageId)
        , Payload(MoveTemp(InPayload))
        , TimeoutSeconds(InTimeoutSeconds)
        , ResultHolder(MakeShared<TGameResult<TRspStruct, ENetRequestResult>, ESPMode::ThreadSafe>(
            TGameResult<TRspStruct, ENetRequestResult>::Failure(ENetRequestResult::SendFailed)
        )) {
    }

    TNetRequestStructAwaiter(const TNetRequestStructAwaiter&) = default;
    TNetRequestStructAwaiter(TNetRequestStructAwaiter&&) noexcept = default;
    TNetRequestStructAwaiter& operator=(const TNetRequestStructAwaiter&) = default;
    TNetRequestStructAwaiter& operator=(TNetRequestStructAwaiter&&) noexcept = default;
    ~TNetRequestStructAwaiter() = default;

    /**
     * @brief 始终挂起等待网络往返与响应反序列化
     * @return 恒返回 false
     */
    [[nodiscard]] constexpr bool await_ready() const noexcept {
        return false;
    }

    /**
     * @brief 挂起协程并派发异步网络请求
     * @param InGuard 挂起点协程帧安全守卫
     */
    void SuspendWithGuard(TSharedRef<Game::Coro::FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard);

    /**
     * @brief 恢复协程并提取网络请求响应结果
     * @return 包含响应结构体或失败状态枚举的 TGameResult
     */
    [[nodiscard]] TGameResult<TRspStruct, ENetRequestResult> await_resume() {
        return MoveTemp(*ResultHolder);
    }

private:
    TWeakObjectPtr<UGameNetServiceSubsystem> SubsystemWeak;
    uint32 RequestMessageId;
    uint32 ExpectedResponseMessageId;
    TReqStruct Payload;
    float TimeoutSeconds;
    TSharedRef<TGameResult<TRspStruct, ENetRequestResult>, ESPMode::ThreadSafe> ResultHolder;
};
