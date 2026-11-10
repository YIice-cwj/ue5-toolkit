/**
 * @file FGameCoroAwaiters.cpp
 * @brief GameCore C++20 协程挂起原语实现
 */

#include "Async/FGameCoroAwaiters.h"

#include "Async/UThrottledTickerSubsystem.h"
#include "Async/UTimeSlicedTaskSubsystem.h"

namespace Game::Coro {

/**
 * @brief 挂起协程并通过 FGameThreadTaskRunner 投递至后台工作线程恢复
 * @param InGuard 挂起点协程帧安全守卫
 */
void FSwitchToBackgroundAwaiter::SuspendWithGuard(
    TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard
) const {
    FGameThreadTaskRunner::Run(
        FCancellationToken::None(),
        [InGuard]() {
            InGuard->ResumeOrAbort();
        }
    );
}

/**
 * @brief 挂起协程并通过 FGameThreadTaskRunner 投递回 GameThread 恢复
 * @param InGuard 挂起点协程帧安全守卫
 */
void FSwitchToGameThreadAwaiter::SuspendWithGuard(
    TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard
) const {
    if (IsInGameThread()) {
        InGuard->ResumeOrAbort();
        return;
    }
    FGameThreadTaskRunner::RunOnGameThread([InGuard]() {
        InGuard->ResumeOrAbort();
    });
}

/**
 * @brief 构造绑定纯 C++ 节流调度器的延时等待器
 * @param InScheduler 节流调度器指针
 * @param InDurationSeconds 等待秒数
 */
FWaitSecondsAwaiter::FWaitSecondsAwaiter(
    FThrottledTickScheduler* InScheduler,
    float InDurationSeconds
) noexcept
    : Scheduler(InScheduler)
    , DurationSeconds(InDurationSeconds) {
}

/**
 * @brief 在节流调度器中注册定时回调，到期后自动注销并恢复协程
 * @param InGuard 挂起点协程帧安全守卫
 */
void FWaitSecondsAwaiter::SuspendWithGuard(
    TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard
) {
    if (Scheduler == nullptr) {
        InGuard->AbortAndDestroyFrame();
        return;
    }
    if (DurationSeconds <= 0.0f) {
        InGuard->ResumeOrAbort();
        return;
    }

    FThrottledTickScheduler* SchedulerPtr = Scheduler;
    const float TargetDuration = DurationSeconds;
    TSharedRef<float, ESPMode::ThreadSafe> ElapsedSeconds = MakeShared<float, ESPMode::ThreadSafe>(0.0f);
    TSharedRef<uint64, ESPMode::ThreadSafe> HandleSlot = MakeShared<uint64, ESPMode::ThreadSafe>(0ULL);

    const uint64 RegisteredHandle = SchedulerPtr->RegisterIntervalTick(
        [InGuard]() {
            return !InGuard->GetState()->ShouldAbort();
        },
        0.0f,
        [SchedulerPtr, TargetDuration, ElapsedSeconds, HandleSlot, InGuard](float DeltaTime) {
            if (InGuard->GetState()->ShouldAbort()) {
                if (*HandleSlot != 0ULL) {
                    SchedulerPtr->UnregisterTick(*HandleSlot);
                    *HandleSlot = 0ULL;
                }
                InGuard->AbortAndDestroyFrame();
                return;
            }
            *ElapsedSeconds += FMath::Max(0.0f, DeltaTime);
            if (*ElapsedSeconds + 1e-5f >= TargetDuration) {
                if (*HandleSlot != 0ULL) {
                    SchedulerPtr->UnregisterTick(*HandleSlot);
                    *HandleSlot = 0ULL;
                }
                InGuard->ResumeOrAbort();
            }
        }
    );

    if (RegisteredHandle == 0ULL) {
        InGuard->AbortAndDestroyFrame();
        return;
    }
    *HandleSlot = RegisteredHandle;
}

/**
 * @brief 构造条件轮询等待器
 * @param InScheduler 节流调度器指针
 * @param InPollIntervalSeconds 轮询间隔秒数
 * @param InPredicate 条件断言闭包（返回 true 时唤醒协程）
 */
FWaitUntilAwaiter::FWaitUntilAwaiter(
    FThrottledTickScheduler* InScheduler,
    float InPollIntervalSeconds,
    TFunction<bool()> InPredicate
)
    : Scheduler(InScheduler)
    , PollIntervalSeconds(FMath::Max(0.0f, InPollIntervalSeconds))
    , Predicate(MoveTemp(InPredicate)) {
}

/**
 * @brief 若条件初始已满足则无需挂起直通执行
 * @return 条件已满足返回 true
 */
bool FWaitUntilAwaiter::await_ready() const {
    return Predicate && Predicate();
}

/**
 * @brief 在节流调度器中按指定间隔轮询条件，达成或取消时立即注销并唤醒
 * @param InGuard 挂起点协程帧安全守卫
 */
void FWaitUntilAwaiter::SuspendWithGuard(
    TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard
) {
    if (Scheduler == nullptr || !Predicate) {
        InGuard->AbortAndDestroyFrame();
        return;
    }

    FThrottledTickScheduler* SchedulerPtr = Scheduler;
    TSharedRef<uint64, ESPMode::ThreadSafe> HandleSlot = MakeShared<uint64, ESPMode::ThreadSafe>(0ULL);

    const uint64 RegisteredHandle = SchedulerPtr->RegisterIntervalTick(
        [InGuard]() {
            return !InGuard->GetState()->ShouldAbort();
        },
        PollIntervalSeconds,
        [SchedulerPtr, HandleSlot, InGuard, Cond = MoveTemp(Predicate)]([[maybe_unused]] float DeltaTime) {
            if (InGuard->GetState()->ShouldAbort()) {
                if (*HandleSlot != 0ULL) {
                    SchedulerPtr->UnregisterTick(*HandleSlot);
                    *HandleSlot = 0ULL;
                }
                InGuard->AbortAndDestroyFrame();
                return;
            }
            if (Cond()) {
                if (*HandleSlot != 0ULL) {
                    SchedulerPtr->UnregisterTick(*HandleSlot);
                    *HandleSlot = 0ULL;
                }
                InGuard->ResumeOrAbort();
            }
        }
    );

    if (RegisteredHandle == 0ULL) {
        InGuard->AbortAndDestroyFrame();
        return;
    }
    *HandleSlot = RegisteredHandle;
}

/**
 * @brief 构造绑定时间切片调度器的让出等待器
 * @param InScheduler 时间切片调度器指针
 */
FYieldTimeSliceAwaiter::FYieldTimeSliceAwaiter(FTimeSliceScheduler* InScheduler) noexcept
    : Scheduler(InScheduler) {
}

/**
 * @brief 将协程单步恢复任务压入 FTimeSliceScheduler
 * @param InGuard 挂起点协程帧安全守卫
 */
void FYieldTimeSliceAwaiter::SuspendWithGuard(
    TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard
) {
    if (Scheduler == nullptr) {
        InGuard->AbortAndDestroyFrame();
        return;
    }

    const uint64 TaskId = Scheduler->EnqueueTask(
        [InGuard]() {
            return !InGuard->GetState()->ShouldAbort();
        },
        [InGuard]() {
            InGuard->ResumeOrAbort();
            return false;
        }
    );

    if (TaskId == 0ULL) {
        InGuard->AbortAndDestroyFrame();
    }
}

/**
 * @brief 基于世界上下文绑定的 UThrottledTickerSubsystem 挂起等待指定秒数
 * @param WorldContextObject 世界上下文对象
 * @param InDurationSeconds 等待秒数
 * @return 延时等待 Awaiter
 */
FWaitSecondsAwaiter WaitSeconds(
    const UObject* WorldContextObject,
    float InDurationSeconds
) noexcept {
    UThrottledTickerSubsystem* Subsystem = UThrottledTickerSubsystem::GetPtr(WorldContextObject);
    return FWaitSecondsAwaiter(Subsystem ? &Subsystem->GetScheduler() : nullptr, InDurationSeconds);
}

/**
 * @brief 基于世界上下文绑定的 UThrottledTickerSubsystem 按固定间隔轮询直至条件满足
 * @param WorldContextObject 世界上下文对象
 * @param InPollIntervalSeconds 轮询间隔秒数
 * @param InPredicate 条件断言闭包
 * @return 条件等待 Awaiter
 */
FWaitUntilAwaiter WaitUntil(
    const UObject* WorldContextObject,
    float InPollIntervalSeconds,
    TFunction<bool()> InPredicate
) {
    UThrottledTickerSubsystem* Subsystem = UThrottledTickerSubsystem::GetPtr(WorldContextObject);
    return FWaitUntilAwaiter(
        Subsystem ? &Subsystem->GetScheduler() : nullptr,
        InPollIntervalSeconds,
        MoveTemp(InPredicate)
    );
}

/**
 * @brief 基于世界上下文绑定的 UTimeSlicedTaskSubsystem 让出当前时间切片
 * @param WorldContextObject 世界上下文对象
 * @return 时间切片让出 Awaiter
 */
FYieldTimeSliceAwaiter YieldTimeSlice(const UObject* WorldContextObject) noexcept {
    UTimeSlicedTaskSubsystem* Subsystem = UTimeSlicedTaskSubsystem::GetPtr(WorldContextObject);
    return FYieldTimeSliceAwaiter(Subsystem ? &Subsystem->GetScheduler() : nullptr);
}

} // namespace Game::Coro
