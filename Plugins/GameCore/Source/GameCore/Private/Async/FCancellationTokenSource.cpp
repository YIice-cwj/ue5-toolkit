/**
 * @file FCancellationTokenSource.cpp
 * @brief 异步取消信号发生源与级联联合取消实现
 */

#include "Async/FCancellationTokenSource.h"

FCancellationTokenSource::FCancellationTokenSource()
    : State(MakeShared<FCancellationState>()) {}

FCancellationToken FCancellationTokenSource::GetToken() const noexcept {
    return FCancellationToken(State);
}

bool FCancellationTokenSource::IsCancellationRequested() const noexcept {
    return State->IsCancellationRequested();
}

bool FCancellationTokenSource::Cancel() {
    return State->RequestCancel();
}

TSharedRef<FCancellationTokenSource> FCancellationTokenSource::CreateLinkedTokenSource(
    const FCancellationToken& TokenA,
    const FCancellationToken& TokenB
) {
    TSharedRef<FCancellationTokenSource> Linked = MakeShared<FCancellationTokenSource>();
    TWeakPtr<FCancellationState> WeakState = Linked->State;

    auto OnAnyCancelled = [WeakState]() {
        if (TSharedPtr<FCancellationState> Pinned = WeakState.Pin()) {
            Pinned->RequestCancel();
        }
    };

    Linked->LinkedRegistrations.Add(TokenA.Register(OnAnyCancelled));
    Linked->LinkedRegistrations.Add(TokenB.Register(OnAnyCancelled));

    return Linked;
}
