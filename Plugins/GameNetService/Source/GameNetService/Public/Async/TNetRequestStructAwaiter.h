/**
 * @file TNetRequestStructAwaiter.h
 * @brief 业务网络 Request-Response 异步结构体请求 C++20 协程挂起器（头文件完全自包含，零响应深拷贝）
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/TGameCoroTask.h"
#include "Dispatch/NetDispatchTypes.h"
#include "Types/TGameResult.h"

/**
 * @brief 挂起协程并等待网络响应的泛型 Awaiter（支持生命周期守卫熔断与零拷贝结果交付）
 * @tparam TReqStruct 请求反射结构体类型
 * @tparam TRspStruct 响应反射结构体类型
 */
template <typename TReqStruct, typename TRspStruct>
class TNetRequestStructAwaiter {
public:
    using FResultType = TGameResult<TRspStruct, ENetRequestResult>;
    using FCompleteCallback = TFunction<void(FResultType)>;
    using FDispatchFunction = TFunction<void(FCompleteCallback)>;

public:
    /**
     * @brief 构造网络结构体请求等待器
     * @param InDispatchFn 协程挂起时触发的请求发起与响应回填闭包
     */
    explicit TNetRequestStructAwaiter(FDispatchFunction InDispatchFn)
        : DispatchFn(MoveTemp(InDispatchFn))
        , ResultHolder(MakeShared<FResultType, ESPMode::ThreadSafe>(
            FResultType::Failure(ENetRequestResult::SendFailed)
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
    void SuspendWithGuard(TSharedRef<Game::Coro::FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard) {
        if (!DispatchFn) {
            InGuard->AbortAndDestroyFrame();
            return;
        }

        TSharedRef<FResultType, ESPMode::ThreadSafe> CapturedSlot = ResultHolder;
        FDispatchFunction LocalDispatch = MoveTemp(DispatchFn);
        LocalDispatch([InGuard, CapturedSlot](FResultType Result) {
            *CapturedSlot = MoveTemp(Result);
            InGuard->ResumeOrAbort();
        });
    }

    /**
     * @brief 恢复协程并移动提取网络请求响应结果
     * @return 包含响应结构体或失败状态枚举的 TGameResult
     */
    [[nodiscard]] FResultType await_resume() {
        return MoveTemp(*ResultHolder);
    }

private:
    FDispatchFunction DispatchFn;
    TSharedRef<FResultType, ESPMode::ThreadSafe> ResultHolder;
};
