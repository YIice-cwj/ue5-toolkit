/**
 * @file TGameCoroTask.h
 * @brief C++20 轻量级生产协程任务句柄（单文件单主类，底层状态与 Promise 拆分于 GameCoroPromise.h）
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/GameCoroPromise.h"

/**
 * @brief C++20 轻量级生产协程任务句柄（支持值返回、void 返回、嵌套 co_await、主动取消与 Fire-and-Forget）
 * @tparam T 协程返回值类型（默认为 void）
 */
template <typename T = void>
class TGameCoroTask {
public:
    using promise_type = Game::Coro::Detail::TGameCoroPromise<T>;

public:
    /**
     * @brief 基于内部共享状态构造协程任务句柄
     * @param InState 共享状态引用
     */
    explicit TGameCoroTask(TSharedRef<TGameCoroState<T>, ESPMode::ThreadSafe> InState) noexcept
        : State(MoveTemp(InState)) {
    }

    /**
     * @brief 查询当前协程状态（Running / Completed / Cancelled）
     * @return 协程状态枚举
     */
    [[nodiscard]] EGameCoroStatus GetStatus() const noexcept {
        return State->GetStatus();
    }

    /**
     * @brief 查询协程是否已成功执行完毕
     * @return 已成功完成返回 true
     */
    [[nodiscard]] bool IsCompleted() const noexcept {
        return State->GetStatus() == EGameCoroStatus::Completed;
    }

    /**
     * @brief 查询协程是否已因取消或宿主销毁而中止
     * @return 已取消返回 true
     */
    [[nodiscard]] bool IsCancelled() const noexcept {
        return State->GetStatus() == EGameCoroStatus::Cancelled;
    }

    /**
     * @brief 查询协程是否已结束（Completed 或 Cancelled）
     * @return 已结束返回 true
     */
    [[nodiscard]] bool IsDone() const noexcept {
        return State->GetStatus() != EGameCoroStatus::Running;
    }

    /**
     * @brief 向协程发送取消请求（将在下一个挂起点恢复前拦截并安全销毁协程帧）
     * @return 首次触发取消返回 true
     */
    bool Cancel() noexcept {
        return State->RequestCancel();
    }

    /**
     * @brief 从外部为协程绑定或更新宿主 UObject 与取消令牌（若绑定时已失效则立即触发取消与挂起帧销毁）
     * @param InOwner 宿主对象指针
     * @param InToken 协同取消令牌
     * @note 本接口为外部事后绑定；若需确保协程首段同步逻辑同样受宿主存活保护，请在协程函数首行使用 co_await Game::Coro::BindContext(InOwner, InToken)
     */
    void BindContext(UObject* InOwner, FCancellationToken InToken = FCancellationToken::None()) noexcept {
        State->BindExecutionContext(TWeakObjectPtr<UObject>(InOwner), true, MoveTemp(InToken));
        if (State->ShouldAbort()) {
            State->RequestCancel();
        }
    }

    /**
     * @brief 获取协程执行结果（Completed 返回 Success，Cancelled 返回 Cancelled 错误码）
     * @return TGameStatusResult<T> 结果对象
     */
    [[nodiscard]] TGameStatusResult<T> GetResult() const {
        return State->GetResult();
    }

    /**
     * @brief 注册当协程结束（完成或取消）时触发的回调（若已结束则立即在当前线程调用）
     * @param InCallback 接收 TGameStatusResult<T> 的回调闭包
     */
    void WhenCompleted(TFunction<void(TGameStatusResult<T>)> InCallback) const {
        if (!InCallback) {
            return;
        }
        TSharedRef<TGameCoroState<T>, ESPMode::ThreadSafe> CapturedState = State;
        State->AddCompletionCallback(
            [CapturedState, Callback = MoveTemp(InCallback)]([[maybe_unused]] EGameCoroStatus FinalStatus) {
                Callback(CapturedState->GetResult());
            }
        );
    }

    /**
     * @brief 获取底层线程安全共享状态引用
     * @return 共享状态引用
     */
    [[nodiscard]] const TSharedRef<TGameCoroState<T>, ESPMode::ThreadSafe>& GetSharedState() const noexcept {
        return State;
    }

private:
    TSharedRef<TGameCoroState<T>, ESPMode::ThreadSafe> State;
};

namespace Game::Coro::Detail {

template <typename T>
template <typename U>
TGuardedAwaiter<TChildTaskAwaiter<U>> TGameCoroPromiseBase<T>::await_transform(const TGameCoroTask<U>& InChildTask) {
    return TGuardedAwaiter<TChildTaskAwaiter<U>>(
        State,
        TChildTaskAwaiter<U>(InChildTask.GetSharedState())
    );
}

template <typename T>
template <typename U>
TGuardedAwaiter<TChildTaskAwaiter<U>> TGameCoroPromiseBase<T>::await_transform(TGameCoroTask<U>&& InChildTask) {
    return TGuardedAwaiter<TChildTaskAwaiter<U>>(
        State,
        TChildTaskAwaiter<U>(InChildTask.GetSharedState())
    );
}

template <typename T>
TGameCoroTask<T> TGameCoroPromise<T>::get_return_object() noexcept {
    return TGameCoroTask<T>(this->State);
}

inline TGameCoroTask<void> TGameCoroPromise<void>::get_return_object() noexcept {
    return TGameCoroTask<void>(this->State);
}

} // namespace Game::Coro::Detail
