/**
 * @file FMessageDispatchQueue.cpp
 * @brief 跨线程无锁 MPSC 消息派发队列实现（ARM64 强一致唤醒协议与主线程缓存弱引用）
 */

#include "Message/FMessageDispatchQueue.h"

#include "Async/Async.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Message/MessageErrorCodes.h"

namespace Game::Message::Private {

/**
 * @brief 构造派发队列并在主线程一次性缓存子系统弱引用
 * @param InOwner 所属消息子系统（必须在主线程传入）
 */
FMessageDispatchQueue::FMessageDispatchQueue(UGameplayMessageSubsystem* InOwner)
    : CachedOwnerWeak(InOwner) {
    check(IsInGameThread());
}

/**
 * @brief 主线程重置队列运行态并排空上个关闭窗口的竞态残留任务（子系统 Initialize 时调用）
 */
void FMessageDispatchQueue::ResetForInitialize() {
    check(IsInGameThread());
    while (PendingQueue.Dequeue().IsSet()) {
    }
    bDrainScheduled.store(false, std::memory_order_seq_cst);
    bIsShuttingDown.store(false, std::memory_order_release);
}

/**
 * @brief 关闭入队通道、封锁后续唤醒调度并排空丢弃队列残留任务（主线程 Deinitialize 时调用）
 */
void FMessageDispatchQueue::Shutdown() {
    check(IsInGameThread());
    bIsShuttingDown.store(true, std::memory_order_seq_cst);
    bDrainScheduled.store(true, std::memory_order_seq_cst);
    while (PendingQueue.Dequeue().IsSet()) {
    }
}

/**
 * @brief 线程安全压入一条待主线程执行的广播任务并合并投递 GameThread 唤醒
 * @param BroadcastTask 待执行任务闭包
 * @return 成功入队返回 Success，关闭态或空任务返回 Failure
 */
TGameStatusResult<void> FMessageDispatchQueue::Enqueue(TUniqueFunction<void()> BroadcastTask) {
    if (bIsShuttingDown.load(std::memory_order_acquire)) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Message,
            Game::MessageErrorCodes::SubsystemShuttingDown,
            TEXT("子系统正在关闭")
        );
    }
    if (!BroadcastTask) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Message,
            Game::MessageErrorCodes::InvalidPayload,
            TEXT("待入队广播任务为空")
        );
    }

    PendingQueue.Enqueue(MoveTemp(BroadcastTask));

    if (bIsShuttingDown.load(std::memory_order_seq_cst)) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Message,
            Game::MessageErrorCodes::SubsystemShuttingDown,
            TEXT("子系统正在关闭")
        );
    }

    bool bExpected = false;
    if (bDrainScheduled.compare_exchange_strong(bExpected, true, std::memory_order_seq_cst)) {
        const TWeakObjectPtr<UGameplayMessageSubsystem> WeakOwner = CachedOwnerWeak;
        AsyncTask(ENamedThreads::GameThread, [WeakOwner]() {
            if (UGameplayMessageSubsystem* Self = WeakOwner.Get()) {
                Self->FlushPendingMessages();
            }
        });
    }

    return TGameStatusResult<void>::Success();
}

/**
 * @brief 在主线程立即排空并串行执行所有已入队广播任务（采用 seq_cst 防止弱内存序丢唤醒）
 * @return 本次实际执行的任务数量
 */
int32 FMessageDispatchQueue::FlushPending() {
    check(IsInGameThread());
    int32 DispatchedCount = 0;

    while (!bIsShuttingDown.load(std::memory_order_acquire)) {
        bDrainScheduled.store(false, std::memory_order_seq_cst);

        while (TOptional<TUniqueFunction<void()>> Task = PendingQueue.Dequeue()) {
            if (bIsShuttingDown.load(std::memory_order_acquire)) {
                return DispatchedCount;
            }
            if (*Task) {
                (*Task)();
                ++DispatchedCount;
            }
        }

        if (PendingQueue.IsEmpty()) {
            break;
        }
    }

    return DispatchedCount;
}

}  // namespace Game::Message::Private
