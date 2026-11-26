/**
 * @file FWaitLoadingCompleteAwaiter.cpp
 * @brief 关卡加载与异步就绪屏障完成 C++20 协程挂起等待器实现
 */

#include "Async/FWaitLoadingCompleteAwaiter.h"

#include <atomic>

#include "Async/FGameThreadTaskRunner.h"
#include "Engine/GameInstance.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "System/ULoadingSubsystem.h"
#include "TimerManager.h"

/**
 * @brief 挂起期跨线程共享状态（隔离委托、定时超时、取消令牌与协程主动取消五路竞态）
 */
struct FWaitLoadingCompleteAwaiter::FWaitLoadingState
    : public TSharedFromThis<FWaitLoadingCompleteAwaiter::FWaitLoadingState, ESPMode::ThreadSafe> {
    FCriticalSection Lock;
    TWeakObjectPtr<ULoadingSubsystem> SubsystemWeak;
    TWeakObjectPtr<UGameInstance> GameInstanceWeak;
    FDelegateHandle StateListenerHandle;
    FDelegateHandle AbortListenerHandle;
    FTimerHandle TimeoutTimerHandle;
    FCancellationTokenRegistration CancelRegistration;
    std::atomic<bool> bCompleted{false};
    std::atomic<bool> bSucceeded{false};
    bool bClosed = false;

    /**
     * @brief 构造挂起共享状态
     * @param InSubsystemWeak 目标加载子系统弱引用
     * @param InGameInstance 所属 GameInstance 指针
     */
    FWaitLoadingState(TWeakObjectPtr<ULoadingSubsystem> InSubsystemWeak, UGameInstance* InGameInstance) noexcept
        : SubsystemWeak(InSubsystemWeak)
        , GameInstanceWeak(InGameInstance) {
    }

    /**
     * @brief 查询该挂起态是否已被任一通道抢占完成
     * @return 已进入终态返回 true
     */
    [[nodiscard]] bool IsCompleted() const noexcept {
        return bCompleted.load(std::memory_order_acquire);
    }

    /**
     * @brief 在互斥锁保护下挂载主线程委托与定时器句柄；若已关闭则立即在主线程清理防泄漏
     * @param InStateHandle 加载状态委托句柄
     * @param InAbortHandle 子系统销毁中止委托句柄
     * @param InTimerHandle 超时定时器句柄
     */
    void AttachGameThreadHandles(
        FDelegateHandle InStateHandle,
        FDelegateHandle InAbortHandle,
        FTimerHandle InTimerHandle
    ) {
        bool bAlreadyClosed = false;
        {
            FScopeLock ScopeLock(&Lock);
            if (bClosed) {
                bAlreadyClosed = true;
            } else {
                StateListenerHandle = InStateHandle;
                AbortListenerHandle = InAbortHandle;
                TimeoutTimerHandle = InTimerHandle;
            }
        }
        if (bAlreadyClosed) {
            ReleaseGameThreadHandles(InStateHandle, InAbortHandle, InTimerHandle);
        }
    }

    /**
     * @brief 在互斥锁保护下挂载取消令牌注销句柄；若已关闭则出锁立即注销防泄漏
     * @param InRegistration 取消令牌注销句柄
     */
    void AttachCancelRegistration(FCancellationTokenRegistration InRegistration) {
        FCancellationTokenRegistration ToUnregister;
        {
            FScopeLock ScopeLock(&Lock);
            if (bClosed) {
                ToUnregister = MoveTemp(InRegistration);
            } else {
                CancelRegistration = MoveTemp(InRegistration);
            }
        }
        ToUnregister.Unregister();
    }

    /**
     * @brief 原子抢占终态、释放全部资源并确保严格在 GameThread 恢复挂起协程
     * @param bInSucceeded 是否为加载正常完成
     * @param Guard 挂起点协程帧安全守卫
     */
    void Complete(bool bInSucceeded, TSharedRef<Game::Coro::FGameCoroSuspensionGuard, ESPMode::ThreadSafe> Guard) {
        if (bCompleted.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        bSucceeded.store(bInSucceeded, std::memory_order_release);
        CloseAndUnregisterToken();

        if (IsInGameThread()) {
            CleanupGameThreadResources();
            Guard->ResumeOrAbort();
            return;
        }

        TSharedRef<FWaitLoadingState, ESPMode::ThreadSafe> Self = AsShared();
        FGameThreadTaskRunner::RunOnGameThread([Self, Guard = MoveTemp(Guard)]() {
            Self->CleanupGameThreadResources();
            Guard->ResumeOrAbort();
        });
    }

    /**
     * @brief 响应 TGameCoroTask::Cancel() 或宿主销毁，原子关闭状态并安全在主线程回收所有监听与定时器
     */
    void AbortFromCoroutineCancel() {
        if (bCompleted.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        bSucceeded.store(false, std::memory_order_release);
        CloseAndUnregisterToken();

        if (IsInGameThread()) {
            CleanupGameThreadResources();
            return;
        }

        TSharedRef<FWaitLoadingState, ESPMode::ThreadSafe> Self = AsShared();
        FGameThreadTaskRunner::RunOnGameThread([Self]() {
            Self->CleanupGameThreadResources();
        });
    }

private:
    /**
     * @brief 加锁置位关闭标记并在锁外注销取消令牌监听
     */
    void CloseAndUnregisterToken() {
        FCancellationTokenRegistration LocalRegistration;
        {
            FScopeLock ScopeLock(&Lock);
            bClosed = true;
            LocalRegistration = MoveTemp(CancelRegistration);
        }
        LocalRegistration.Unregister();
    }

    /**
     * @brief 取出并在锁外释放所有主线程委托与定时器资源（须在 GameThread 调用）
     */
    void CleanupGameThreadResources() {
        FDelegateHandle LocalStateHandle;
        FDelegateHandle LocalAbortHandle;
        FTimerHandle LocalTimerHandle;
        {
            FScopeLock ScopeLock(&Lock);
            LocalStateHandle = StateListenerHandle;
            LocalAbortHandle = AbortListenerHandle;
            LocalTimerHandle = TimeoutTimerHandle;
            StateListenerHandle.Reset();
            AbortListenerHandle.Reset();
            TimeoutTimerHandle.Invalidate();
        }
        ReleaseGameThreadHandles(LocalStateHandle, LocalAbortHandle, LocalTimerHandle);
    }

    /**
     * @brief 向主线程定时器管理器与加载子系统注销指定句柄集合
     * @param InStateHandle 加载状态委托句柄
     * @param InAbortHandle 子系统销毁中止委托句柄
     * @param InTimerHandle 超时定时器句柄
     */
    void ReleaseGameThreadHandles(
        FDelegateHandle InStateHandle,
        FDelegateHandle InAbortHandle,
        FTimerHandle InTimerHandle
    ) const {
        if (UGameInstance* GameInstance = GameInstanceWeak.Get()) {
            if (InTimerHandle.IsValid()) {
                GameInstance->GetTimerManager().ClearTimer(InTimerHandle);
            }
        }
        if (ULoadingSubsystem* Subsystem = SubsystemWeak.Get()) {
            if (InStateHandle.IsValid()) {
                Subsystem->OnLoadingStateChangedNative.Remove(InStateHandle);
            }
            if (InAbortHandle.IsValid()) {
                Subsystem->OnLoadingAbortedNative.Remove(InAbortHandle);
            }
        }
    }
};

FWaitLoadingCompleteAwaiter::FWaitLoadingCompleteAwaiter(
    ULoadingSubsystem* InSubsystem,
    FCancellationToken InCancellationToken,
    float InTimeoutSeconds
) noexcept
    : SubsystemWeak(InSubsystem)
    , CancellationToken(MoveTemp(InCancellationToken))
    , TimeoutSeconds((FMath::IsFinite(InTimeoutSeconds) && InTimeoutSeconds > 0.0f) ? InTimeoutSeconds : 0.0f) {
}

FWaitLoadingCompleteAwaiter::FWaitLoadingCompleteAwaiter(FWaitLoadingCompleteAwaiter&& Other) noexcept = default;

FWaitLoadingCompleteAwaiter& FWaitLoadingCompleteAwaiter::operator=(
    FWaitLoadingCompleteAwaiter&& Other
) noexcept = default;

FWaitLoadingCompleteAwaiter::~FWaitLoadingCompleteAwaiter() = default;

bool FWaitLoadingCompleteAwaiter::await_ready() const noexcept {
    if (CancellationToken.IsCancellationRequested()) {
        bReadyResult = false;
        return true;
    }
    const ULoadingSubsystem* Subsystem = SubsystemWeak.Get();
    if (Subsystem == nullptr) {
        bReadyResult = false;
        return true;
    }
    if (!Subsystem->IsLoading()) {
        bReadyResult = true;
        return true;
    }
    return false;
}

void FWaitLoadingCompleteAwaiter::SuspendWithGuard(
    TSharedRef<Game::Coro::FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard
) {
    if (CancellationToken.IsCancellationRequested()) {
        bReadyResult = false;
        InGuard->ResumeOrAbort();
        return;
    }

    ULoadingSubsystem* Subsystem = SubsystemWeak.Get();
    if (Subsystem == nullptr) {
        bReadyResult = false;
        InGuard->ResumeOrAbort();
        return;
    }
    if (!Subsystem->IsLoading()) {
        bReadyResult = true;
        InGuard->ResumeOrAbort();
        return;
    }

    const TSharedRef<FWaitLoadingState, ESPMode::ThreadSafe> State =
        MakeShared<FWaitLoadingState, ESPMode::ThreadSafe>(SubsystemWeak, Subsystem->GetGameInstance());
    WaitState = State;

    InGuard->GetState()->AddCompletionCallback([State](EGameCoroStatus FinalStatus) {
        if (FinalStatus == EGameCoroStatus::Cancelled) {
            State->AbortFromCoroutineCancel();
        }
    });

    const FDelegateHandle StateHandle = Subsystem->OnLoadingStateChangedNative.AddLambda(
        [State, InGuard](bool bIsLoading, FName) {
            if (!bIsLoading) {
                State->Complete(true, InGuard);
            }
        }
    );

    const FDelegateHandle AbortHandle = Subsystem->OnLoadingAbortedNative.AddLambda(
        [State, InGuard]() {
            State->Complete(false, InGuard);
        }
    );

    FTimerHandle TimerHandle;
    if (TimeoutSeconds > 0.0f) {
        if (UGameInstance* GameInstance = Subsystem->GetGameInstance()) {
            GameInstance->GetTimerManager().SetTimer(
                TimerHandle,
                [State, InGuard]() {
                    State->Complete(false, InGuard);
                },
                TimeoutSeconds,
                false
            );
        }
    }

    State->AttachGameThreadHandles(StateHandle, AbortHandle, TimerHandle);

    if (!State->IsCompleted() && CancellationToken.CanBeCancelled()) {
        State->AttachCancelRegistration(CancellationToken.Register(
            [State, InGuard]() {
                State->Complete(false, InGuard);
            }
        ));
    }
}

bool FWaitLoadingCompleteAwaiter::await_resume() const noexcept {
    if (WaitState.IsValid()) {
        return WaitState->bSucceeded.load(std::memory_order_acquire);
    }
    return bReadyResult;
}

namespace GameFlow {

FWaitLoadingCompleteAwaiter WaitLoadingComplete(
    const UObject* WorldContextObject,
    FCancellationToken CancellationToken,
    float TimeoutSeconds
) {
    return FWaitLoadingCompleteAwaiter(
        ULoadingSubsystem::Get(WorldContextObject),
        MoveTemp(CancellationToken),
        TimeoutSeconds
    );
}

} // namespace GameFlow
