/**
 * @file FWaitLoadingCompleteAwaiter.cpp
 * @brief 关卡加载与异步就绪屏障完成 C++20 协程挂起等待器实现
 */

#include "Async/FWaitLoadingCompleteAwaiter.h"

#include "Engine/GameInstance.h"
#include "System/ULoadingSubsystem.h"
#include "TimerManager.h"

FWaitLoadingCompleteAwaiter::FWaitLoadingCompleteAwaiter(
    ULoadingSubsystem* InSubsystem,
    float InTimeoutSeconds
) noexcept
    : SubsystemWeak(InSubsystem)
    , TimeoutSeconds(InTimeoutSeconds)
    , ResultHolder(MakeShared<bool, ESPMode::ThreadSafe>(false)) {
}

bool FWaitLoadingCompleteAwaiter::await_ready() const noexcept {
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
    };

    const TSharedRef<FAwaiterContext> Context = MakeShared<FAwaiterContext>();
    Context->SubsystemWeak = SubsystemWeak;
    Context->GameInstanceWeak = Subsystem->GetGameInstance();
    Context->ResultHolder = ResultHolder;

    Context->ListenerHandle = Subsystem->OnLoadingStateChangedNative.AddLambda(
        [InGuard, Context](bool bIsLoading, FName) {
            if (!bIsLoading) {
                *Context->ResultHolder = true;
                if (UGameInstance* GI = Context->GameInstanceWeak.Get()) {
                    GI->GetTimerManager().ClearTimer(Context->TimeoutTimerHandle);
                }
                const TSharedRef<Game::Coro::FGameCoroSuspensionGuard, ESPMode::ThreadSafe> GuardCopy = InGuard;
                const TSharedRef<FAwaiterContext> ContextCopy = Context;
                GuardCopy->ResumeOrAbort();
                if (ULoadingSubsystem* ValidSubsystem = ContextCopy->SubsystemWeak.Get()) {
                    ValidSubsystem->OnLoadingStateChangedNative.Remove(ContextCopy->ListenerHandle);
                }
            }
        }
    );

    if (TimeoutSeconds > 0.0f) {
        if (UGameInstance* GI = Subsystem->GetGameInstance()) {
            GI->GetTimerManager().SetTimer(
                Context->TimeoutTimerHandle,
                [InGuard, Context]() {
                    *Context->ResultHolder = false;
                    const TSharedRef<Game::Coro::FGameCoroSuspensionGuard, ESPMode::ThreadSafe> GuardCopy = InGuard;
                    const TSharedRef<FAwaiterContext> ContextCopy = Context;
                    GuardCopy->ResumeOrAbort();
                    if (ULoadingSubsystem* ValidSubsystem = ContextCopy->SubsystemWeak.Get()) {
                        ValidSubsystem->OnLoadingStateChangedNative.Remove(ContextCopy->ListenerHandle);
                    }
                },
                TimeoutSeconds,
                false
            );
        }
    }
}

bool FWaitLoadingCompleteAwaiter::await_resume() const noexcept {
    return ResultHolder.IsValid() && *ResultHolder;
}

namespace GameFlow {

FWaitLoadingCompleteAwaiter WaitLoadingComplete(
    const UObject* WorldContextObject,
    float TimeoutSeconds
) {
    return FWaitLoadingCompleteAwaiter(ULoadingSubsystem::Get(WorldContextObject), TimeoutSeconds);
}

} // namespace GameFlow
