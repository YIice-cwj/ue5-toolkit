/**
 * @file FCancellationTokenSource.cpp
 * @brief 异步取消信号发生源与级联联合取消实现
 */

#include "Async/FCancellationTokenSource.h"

/**
 * @brief 构造包含全新独立共享状态的取消源实例
 */
FCancellationTokenSource::FCancellationTokenSource()
    : State(MakeShared<FCancellationState>()) {}

/**
 * @brief 获取与当前取消源关联的值语义只读取消令牌
 * @return 只读取消令牌
 */
FCancellationToken FCancellationTokenSource::GetToken() const noexcept {
    return FCancellationToken(State);
}

/**
 * @brief 判定当前取消源是否已被触发取消
 * @return 已触发返回 true
 */
bool FCancellationTokenSource::IsCancellationRequested() const noexcept {
    return State->IsCancellationRequested();
}

/**
 * @brief 广播触发取消信号并出锁派发所有关联回调
 * @return 首次触发返回 true，重复调用返回 false
 */
bool FCancellationTokenSource::Cancel() {
    return State->RequestCancel();
}

/**
 * @brief 创建级联联合取消源（任一输入令牌触发取消时，衍生源自动被触发取消）
 * @param TokenA 第一输入令牌
 * @param TokenB 第二输入令牌
 * @return 联动派生出的共享取消源引用
 */
TSharedRef<FCancellationTokenSource> FCancellationTokenSource::CreateLinkedTokenSource(
    const FCancellationToken& TokenA,
    const FCancellationToken& TokenB
) {
    const FCancellationToken TokenArray[2] = { TokenA, TokenB };
    return CreateLinkedTokenSource(MakeArrayView(TokenArray));
}

/**
 * @brief 创建级联联合取消源（任一输入令牌触发取消时，衍生源自动被触发取消）
 * @param Tokens 输入令牌数组切片
 * @return 联动派生出的共享取消源引用
 */
TSharedRef<FCancellationTokenSource> FCancellationTokenSource::CreateLinkedTokenSource(
    TConstArrayView<FCancellationToken> Tokens
) {
    TSharedRef<FCancellationTokenSource> Linked = MakeShared<FCancellationTokenSource>();
    for (const FCancellationToken& Token : Tokens) {
        if (Token.IsCancellationRequested()) {
            Linked->Cancel();
            return Linked;
        }
    }

    TWeakPtr<FCancellationState> WeakState = Linked->State;
    auto OnAnyCancelled = [WeakState]() {
        if (TSharedPtr<FCancellationState> Pinned = WeakState.Pin()) {
            Pinned->RequestCancel();
        }
    };

    Linked->LinkedRegistrations.Reserve(Tokens.Num());
    for (const FCancellationToken& Token : Tokens) {
        Linked->LinkedRegistrations.Add(Token.Register(OnAnyCancelled));
    }

    return Linked;
}
