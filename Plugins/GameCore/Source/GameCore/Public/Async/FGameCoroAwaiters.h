/**
 * @file FGameCoroAwaiters.h
 * @brief GameCore C++20 协程挂起原语集（主线程/后台线程切换、帧延时等待、条件轮询与时间切片让出）
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/TGameCoroTask.h"
#include "Async/FGameThreadTaskRunner.h"
#include "Async/FThrottledTickScheduler.h"
#include "Async/FTimeSliceScheduler.h"

namespace Game::Coro {

/**
 * @brief 将当前协程切往后台工作线程继续执行的 Awaiter
 */
struct GAMECORE_API FSwitchToBackgroundAwaiter {
    /**
     * @brief 始终挂起并投递至后台工作线程
     * @return 恒返回 false
     */
    [[nodiscard]] constexpr bool await_ready() const noexcept {
        return false;
    }

    /**
     * @brief 挂起协程并通过 FGameThreadTaskRunner 投递至后台工作线程恢复
     * @param InGuard 挂起点协程帧安全守卫
     */
    void SuspendWithGuard(TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard) const;

    /**
     * @brief 恢复执行空操作
     */
    constexpr void await_resume() const noexcept {
    }
};

/**
 * @brief 将当前协程切回 GameThread 主线程继续执行的 Awaiter（若当前已在 GameThread 则零开销直通）
 */
struct GAMECORE_API FSwitchToGameThreadAwaiter {
    /**
     * @brief 若当前已处于 GameThread 则无需挂起直通执行
     * @return 在 GameThread 返回 true
     */
    [[nodiscard]] bool await_ready() const noexcept {
        return IsInGameThread();
    }

    /**
     * @brief 挂起协程并通过 FGameThreadTaskRunner 投递回 GameThread 恢复
     * @param InGuard 挂起点协程帧安全守卫
     */
    void SuspendWithGuard(TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard) const;

    /**
     * @brief 恢复执行空操作
     */
    constexpr void await_resume() const noexcept {
    }
};

/**
 * @brief 在后台工作线程执行计算闭包并自动切回原线程（若发起自 GameThread 则自动回 GameThread）交付结果的 Awaiter
 * @tparam TResult 后台计算结果类型
 */
template <typename TResult>
class TRunOnBackgroundAwaiter {
public:
    /**
     * @brief 构造后台计算等待器
     * @param InWork 后台执行闭包
     */
    explicit TRunOnBackgroundAwaiter(TFunction<TResult()> InWork)
        : Work(MoveTemp(InWork))
        , bReturnToGameThread(IsInGameThread())
        , ResultSlot(MakeShared<TOptional<TResult>, ESPMode::ThreadSafe>()) {
    }

    /**
     * @brief 始终挂起以投递至后台线程执行
     * @return 恒返回 false
     */
    [[nodiscard]] constexpr bool await_ready() const noexcept {
        return false;
    }

    /**
     * @brief 挂起协程并在后台线程计算完成后按需切回 GameThread 恢复
     * @param InGuard 挂起点协程帧安全守卫
     */
    void SuspendWithGuard(TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard) {
        if (!Work) {
            InGuard->AbortAndDestroyFrame();
            return;
        }
        TSharedRef<TOptional<TResult>, ESPMode::ThreadSafe> CapturedSlot = ResultSlot;
        const bool bHopBackToGameThread = bReturnToGameThread;
        FGameThreadTaskRunner::Run(
            FCancellationToken::None(),
            [InGuard, CapturedSlot, bHopBackToGameThread, BgWork = MoveTemp(Work)]() mutable {
                if (InGuard->GetState()->ShouldAbort()) {
                    InGuard->AbortAndDestroyFrame();
                    return;
                }
                CapturedSlot->Emplace(BgWork());
                if (InGuard->GetState()->ShouldAbort()) {
                    InGuard->AbortAndDestroyFrame();
                    return;
                }
                if (bHopBackToGameThread && !IsInGameThread()) {
                    FGameThreadTaskRunner::RunOnGameThread([InGuard]() {
                        InGuard->ResumeOrAbort();
                    });
                } else {
                    InGuard->ResumeOrAbort();
                }
            }
        );
    }

    /**
     * @brief 恢复后移动提取后台计算结果
     * @return 计算结果实例
     */
    [[nodiscard]] TResult await_resume() {
        check(ResultSlot->IsSet());
        return MoveTemp(ResultSlot->GetValue());
    }

private:
    TFunction<TResult()> Work;
    bool bReturnToGameThread = true;
    TSharedRef<TOptional<TResult>, ESPMode::ThreadSafe> ResultSlot;
};

/**
 * @brief 基于 FThrottledTickScheduler 的帧延时等待 Awaiter
 */
class GAMECORE_API FWaitSecondsAwaiter {
public:
    /**
     * @brief 构造绑定纯 C++ 节流调度器的延时等待器
     * @param InScheduler 节流调度器指针
     * @param InDurationSeconds 等待秒数
     */
    FWaitSecondsAwaiter(FThrottledTickScheduler* InScheduler, float InDurationSeconds) noexcept;

    /**
     * @brief 若延时小于等于 0 则无需挂起
     * @return DurationSeconds <= 0 返回 true
     */
    [[nodiscard]] bool await_ready() const noexcept {
        return DurationSeconds <= 0.0f;
    }

    /**
     * @brief 在节流调度器中注册定时回调，到期后自动注销并恢复协程
     * @param InGuard 挂起点协程帧安全守卫
     */
    void SuspendWithGuard(TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard);

    /**
     * @brief 恢复执行空操作
     */
    constexpr void await_resume() const noexcept {
    }

private:
    FThrottledTickScheduler* Scheduler = nullptr;
    float DurationSeconds = 0.0f;
};

/**
 * @brief 基于 FThrottledTickScheduler 的低频条件轮询等待 Awaiter
 */
class GAMECORE_API FWaitUntilAwaiter {
public:
    /**
     * @brief 构造条件轮询等待器
     * @param InScheduler 节流调度器指针
     * @param InPollIntervalSeconds 轮询间隔秒数
     * @param InPredicate 条件断言闭包（返回 true 时唤醒协程）
     */
    FWaitUntilAwaiter(
        FThrottledTickScheduler* InScheduler,
        float InPollIntervalSeconds,
        TFunction<bool()> InPredicate
    );

    /**
     * @brief 若条件初始已满足则无需挂起直通执行
     * @return 条件已满足返回 true
     */
    [[nodiscard]] bool await_ready() const;

    /**
     * @brief 在节流调度器中按指定间隔轮询条件，达成或取消时立即注销并唤醒
     * @param InGuard 挂起点协程帧安全守卫
     */
    void SuspendWithGuard(TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard);

    /**
     * @brief 恢复执行空操作
     */
    constexpr void await_resume() const noexcept {
    }

private:
    FThrottledTickScheduler* Scheduler = nullptr;
    float PollIntervalSeconds = 0.0f;
    TFunction<bool()> Predicate;
};

/**
 * @brief 基于 FTimeSliceScheduler 的帧预算时间切片让出 Awaiter
 */
class GAMECORE_API FYieldTimeSliceAwaiter {
public:
    /**
     * @brief 构造绑定时间切片调度器的让出等待器
     * @param InScheduler 时间切片调度器指针
     */
    explicit FYieldTimeSliceAwaiter(FTimeSliceScheduler* InScheduler) noexcept;

    /**
     * @brief 始终挂起以交由时间切片调度器根据当前帧预算决定当帧继续或跨帧恢复
     * @return 恒返回 false
     */
    [[nodiscard]] constexpr bool await_ready() const noexcept {
        return false;
    }

    /**
     * @brief 将协程单步恢复任务压入 FTimeSliceScheduler
     * @param InGuard 挂起点协程帧安全守卫
     */
    void SuspendWithGuard(TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard);

    /**
     * @brief 恢复执行空操作
     */
    constexpr void await_resume() const noexcept {
    }

private:
    FTimeSliceScheduler* Scheduler = nullptr;
};

/**
 * @brief 在协程内绑定宿主 UObject 弱引用与可选协同取消令牌（宿主为空或已销毁时在挂起点立即自毁协程帧）
 * @param InOwner 宿主 UObject 指针
 * @param InToken 可选外部协同取消令牌
 * @return 上下文绑定描述符
 */
[[nodiscard]] inline FGameCoroContextBinding BindContext(
    UObject* InOwner,
    FCancellationToken InToken = FCancellationToken::None()
) noexcept {
    FGameCoroContextBinding Binding;
    Binding.Owner = TWeakObjectPtr<UObject>(InOwner);
    Binding.Token = MoveTemp(InToken);
    Binding.bHasOwner = true;
    return Binding;
}

/**
 * @brief 在无 UObject 宿主的纯 C++ 协程内绑定协同取消令牌
 * @param InToken 协同取消令牌
 * @return 上下文绑定描述符
 */
[[nodiscard]] inline FGameCoroContextBinding BindCancellationToken(FCancellationToken InToken) noexcept {
    FGameCoroContextBinding Binding;
    Binding.Token = MoveTemp(InToken);
    Binding.bHasOwner = false;
    return Binding;
}

/**
 * @brief 挂起当前协程并切往后台工作线程继续执行
 * @return 后台线程切换 Awaiter
 */
[[nodiscard]] inline FSwitchToBackgroundAwaiter SwitchToBackground() noexcept {
    return FSwitchToBackgroundAwaiter{};
}

/**
 * @brief 挂起当前协程并切回 GameThread 主线程继续执行（若已在 GameThread 则零开销直通）
 * @return 主线程切换 Awaiter
 */
[[nodiscard]] inline FSwitchToGameThreadAwaiter SwitchToGameThread() noexcept {
    return FSwitchToGameThreadAwaiter{};
}

/**
 * @brief 在后台工作线程执行带返回值闭包并在完成后自动切回发起线程交付结果
 * @tparam TResult 闭包返回值类型
 * @param InWork 后台计算闭包
 * @return 后台计算 Awaiter
 */
template <typename TResult>
[[nodiscard]] TRunOnBackgroundAwaiter<TResult> RunOnBackground(TFunction<TResult()> InWork) {
    return TRunOnBackgroundAwaiter<TResult>(MoveTemp(InWork));
}

/**
 * @brief 基于纯 C++ 节流调度器挂起等待指定秒数
 * @param InScheduler 节流调度器引用
 * @param InDurationSeconds 等待秒数
 * @return 延时等待 Awaiter
 */
[[nodiscard]] inline FWaitSecondsAwaiter WaitSeconds(
    FThrottledTickScheduler& InScheduler,
    float InDurationSeconds
) noexcept {
    return FWaitSecondsAwaiter(&InScheduler, InDurationSeconds);
}

/**
 * @brief 基于世界上下文绑定的 UThrottledTickerSubsystem 挂起等待指定秒数
 * @param WorldContextObject 世界上下文对象
 * @param InDurationSeconds 等待秒数
 * @return 延时等待 Awaiter
 */
[[nodiscard]] GAMECORE_API FWaitSecondsAwaiter WaitSeconds(
    const UObject* WorldContextObject,
    float InDurationSeconds
) noexcept;

/**
 * @brief 基于纯 C++ 节流调度器按固定间隔轮询直至条件满足
 * @param InScheduler 节流调度器引用
 * @param InPollIntervalSeconds 轮询间隔秒数
 * @param InPredicate 条件断言闭包
 * @return 条件等待 Awaiter
 */
[[nodiscard]] inline FWaitUntilAwaiter WaitUntil(
    FThrottledTickScheduler& InScheduler,
    float InPollIntervalSeconds,
    TFunction<bool()> InPredicate
) {
    return FWaitUntilAwaiter(&InScheduler, InPollIntervalSeconds, MoveTemp(InPredicate));
}

/**
 * @brief 基于世界上下文绑定的 UThrottledTickerSubsystem 按固定间隔轮询直至条件满足
 * @param WorldContextObject 世界上下文对象
 * @param InPollIntervalSeconds 轮询间隔秒数
 * @param InPredicate 条件断言闭包
 * @return 条件等待 Awaiter
 */
[[nodiscard]] GAMECORE_API FWaitUntilAwaiter WaitUntil(
    const UObject* WorldContextObject,
    float InPollIntervalSeconds,
    TFunction<bool()> InPredicate
);

/**
 * @brief 基于纯 C++ 时间切片调度器让出当前步骤（超帧预算时自动跨帧恢复）
 * @param InScheduler 时间切片调度器引用
 * @return 时间切片让出 Awaiter
 */
[[nodiscard]] inline FYieldTimeSliceAwaiter YieldTimeSlice(FTimeSliceScheduler& InScheduler) noexcept {
    return FYieldTimeSliceAwaiter(&InScheduler);
}

/**
 * @brief 基于世界上下文绑定的 UTimeSlicedTaskSubsystem 让出当前时间切片
 * @param WorldContextObject 世界上下文对象
 * @return 时间切片让出 Awaiter
 */
[[nodiscard]] GAMECORE_API FYieldTimeSliceAwaiter YieldTimeSlice(const UObject* WorldContextObject) noexcept;

} // namespace Game::Coro
