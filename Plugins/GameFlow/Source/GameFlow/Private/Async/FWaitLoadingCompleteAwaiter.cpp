/**
 * @file FWaitLoadingCompleteAwaiter.cpp
 * @brief 关卡加载与异步就绪屏障完成 C++20 协程挂起等待器实现
 */

#include "Async/FWaitLoadingCompleteAwaiter.h"

#include <atomic>

#include "Engine/GameInstance.h"
#include "System/ULoadingSubsystem.h"
#include "TimerManager.h"

FWaitLoadingCompleteAwaiter::FWaitLoadingCompleteAwaiter(
    ULoadingSubsystem* InSubsystem,
    float InTimeoutSeconds,
    FCancellationToken InCancellationToken
) noexcept
    : SubsystemWeak(InSubsystem)
    , TimeoutSeconds(InTimeoutSeconds)
    , CancellationToken(MoveTemp(InCancellationToken))
    , ResultHolder(MakeShared<bool, ESPMode::ThreadSafe>(false)) {
}

bool FWaitLoadingCompleteAwaiter::await_ready() const noexcept {
    if (CancellationToken.IsCancellationRequested()) {
        *ResultHolder = false;
        return true;
    }
    const ULoadingSubsystem* Subsystem = SubsystemWeak.Get();
    if (Subsystem == nullptr) {
        *ResultHolder = false;
        return true;
    }
    if (!Subsystem->IsLoading()) {
        *ResultHolder = true;
        return true;
    }
    return false;
}

void FWaitLoadingCompleteAwaiter::SuspendWithGuard(
    TSharedRef<Game::Coro::FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard
) {
    if (CancellationToken.IsCancellationRequested()) {
        *ResultHolder = false;
        InGuard->ResumeOrAbort();
        return;
    }

    ULoadingSubsystem* Subsystem = SubsystemWeak.Get();
    if (Subsystem == nullptr) {
        *ResultHolder = false;
        InGuard->ResumeOrAbort();
        return;
    }
    if (!Subsystem->IsLoading()) {
        *ResultHolder = true;
        InGuard->ResumeOrAbort();
        return;
    }

    struct FAwaiterContext {
        TWeakObjectPtr<ULoadingSubsystem> SubsystemWeak;
        FDelegateHandle ListenerHandle;
        FTimerHandle TimeoutTimerHandle;
        TWeakObjectPtr<UGameInstance> GameInstanceWeak;
        TSharedPtr<bool, ESPMode::ThreadSafe> ResultHolder;
        FCancellationTokenRegistration CancelRegistration;
        std::atomic<bool> bCompleted{false};
    };

    const TSharedRef<FAwaiterContext> Context = MakeShared<FAwaiterContext>();
    Context->SubsystemWeak = SubsystemWeak;
    Context->GameInstanceWeak = Subsystem->GetGameInstance();
    Context->ResultHolder = ResultHolder;

    auto CleanupAndResume = [Context, InGuard](bool bSuccess) {
        if (Context->bCompleted.exchange(true)) {
            return;
        }
        const TSharedRef<Game::Coro::FGameCoroSuspensionGuard, ESPMode::ThreadSafe> GuardCopy = InGuard;
        const TSharedRef<FAwaiterContext> ContextCopy = Context;

        *ContextCopy->ResultHolder = bSuccess;
        ContextCopy->CancelRegistration.Unregister();
        if (UGameInstance* GI = ContextCopy->GameInstanceWeak.Get()) {
            GI->GetTimerManager().ClearTimer(ContextCopy->TimeoutTimerHandle);
        }

        GuardCopy->ResumeOrAbort();

        if (ULoadingSubsystem* ValidSubsystem = ContextCopy->SubsystemWeak.Get()) {
            if (ContextCopy->ListenerHandle.IsValid()) {
                const FDelegateHandle HandleToRemove = ContextCopy->ListenerHandle;
                ContextCopy->ListenerHandle.Reset();
                ValidSubsystem->OnLoadingStateChangedNative.Remove(HandleToRemove);
            }
        }
    };

    Context->ListenerHandle = Subsystem->OnLoadingStateChangedNative.AddLambda(
        [CleanupAndResume](bool bIsLoading, FName) {
            if (!bIsLoading) {
                CleanupAndResume(true);
            }
        }
    );

    if (TimeoutSeconds > 0.0f) {
        if (UGameInstance* GI = Subsystem->GetGameInstance()) {
            GI->GetTimerManager().SetTimer(
                Context->TimeoutTimerHandle,
                [CleanupAndResume]() {
                    CleanupAndResume(false);
                },
                TimeoutSeconds,
                false
            );
        }
    }

    if (CancellationToken.CanBeCancelled()) {
        Context->CancelRegistration = CancellationToken.Register(
            [CleanupAndResume]() {
                CleanupAndResume(false);
            }
        );
    }
}

bool FWaitLoadingCompleteAwaiter::await_resume() const noexcept {
    return ResultHolder.IsValid() && *ResultHolder;
}

namespace GameFlow {

FWaitLoadingCompleteAwaiter WaitLoadingComplete(
    const UObject* WorldContextObject,
    float TimeoutSeconds,
    FCancellationToken CancellationToken
) {
    return FWaitLoadingCompleteAwaiter(
        ULoadingSubsystem::Get(WorldContextObject),
        TimeoutSeconds,
        MoveTemp(CancellationToken)
    );
}

} // namespace GameFlow
