/**
 * @file FCancellationToken.cpp
 * @brief 异步取消令牌与取消源实现（严格遵循出锁派发铁律）
 */

#include "Async/FCancellationToken.h"

/**
 * @brief 构造注销控制句柄
 * @param InState 状态弱引用
 * @param InCallbackId 回调序号
 */
FCancellationTokenRegistration::FCancellationTokenRegistration(
    TWeakPtr<FCancellationState> InState,
    uint64 InCallbackId
) noexcept
    : WeakState(MoveTemp(InState))
    , CallbackId(InCallbackId) {}

/**
 * @brief 析构时自动解绑回调
 */
FCancellationTokenRegistration::~FCancellationTokenRegistration() {
    Unregister();
}

/**
 * @brief 移动构造函数
 * @param Other 源句柄
 */
FCancellationTokenRegistration::FCancellationTokenRegistration(FCancellationTokenRegistration&& Other) noexcept
    : WeakState(MoveTemp(Other.WeakState))
    , CallbackId(Other.CallbackId) {
    Other.CallbackId = 0;
}

/**
 * @brief 移动赋值操作符
 * @param Other 源句柄
 * @return 自身引用
 */
FCancellationTokenRegistration& FCancellationTokenRegistration::operator=(FCancellationTokenRegistration&& Other) noexcept {
    if (this != &Other) {
        Unregister();
        WeakState = MoveTemp(Other.WeakState);
        CallbackId = Other.CallbackId;
        Other.CallbackId = 0;
    }
    return *this;
}

/**
 * @brief 主动解绑注销回调
 */
void FCancellationTokenRegistration::Unregister() noexcept {
    if (CallbackId != 0) {
        if (TSharedPtr<FCancellationState> Pinned = WeakState.Pin()) {
            Pinned->UnregisterCallback(CallbackId);
        }
        CallbackId = 0;
    }
}

/**
 * @brief 触发取消并无锁派发所有已注册的回调
 * @return 首次触发返回 true，重复调用返回 false
 */
bool FCancellationState::RequestCancel() {
    bool bExpected = false;
    if (!bCancelled.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel)) {
        return false;
    }

    TArray<FCallbackEntry> PendingCallbacks;
    {
        FWriteScopeLock WriteLock(Lock);
        PendingCallbacks = MoveTemp(Callbacks);
    }

    for (const FCallbackEntry& Entry : PendingCallbacks) {
        if (Entry.Action) {
            Entry.Action();
        }
    }

    return true;
}

/**
 * @brief 注册取消监听回调
 * @param Callback 待派发的回调闭包
 * @return 注销控制句柄
 */
FCancellationTokenRegistration FCancellationState::RegisterCallback(TFunction<void()> Callback) {
    if (!Callback) {
        return FCancellationTokenRegistration();
    }

    if (bCancelled.load(std::memory_order_acquire)) {
        Callback();
        return FCancellationTokenRegistration();
    }

    bool bCallImmediately = false;
    uint64 AssignedId = 0;

    {
        FWriteScopeLock WriteLock(Lock);
        if (bCancelled.load(std::memory_order_relaxed)) {
            bCallImmediately = true;
        } else {
            AssignedId = NextCallbackId++;
            Callbacks.Add(FCallbackEntry{AssignedId, MoveTemp(Callback)});
        }
    }

    if (bCallImmediately) {
        Callback();
        return FCancellationTokenRegistration();
    }

    return FCancellationTokenRegistration(AsWeak(), AssignedId);
}

/**
 * @brief 根据序号移除已注册的回调
 * @param CallbackId 目标回调序号
 */
void FCancellationState::UnregisterCallback(uint64 CallbackId) noexcept {
    if (CallbackId == 0) {
        return;
    }

    FWriteScopeLock WriteLock(Lock);
    Callbacks.RemoveAllSwap([CallbackId](const FCallbackEntry& Entry) noexcept {
        return Entry.Id == CallbackId;
    });
}
