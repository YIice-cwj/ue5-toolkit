/**
 * @file FMessageDispatchQueue.h
 * @brief 跨线程无锁 MPSC 消息派发队列声明（负责 ARM64 seq_cst 防丢唤醒与关闭栅栏）
 */

#pragma once

#include <atomic>

#include "Containers/MpscQueue.h"
#include "CoreMinimal.h"
#include "Types/TGameResult.h"
#include "UObject/WeakObjectPtr.h"

class UGameplayMessageSubsystem;

namespace Game::Message::Private {

/**
 * @brief 跨线程安全入队与主线程合并排空调度器
 */
class FMessageDispatchQueue final {
public:
    /**
     * @brief 构造派发队列并在主线程一次性缓存子系统弱引用
     * @param InOwner 所属消息子系统（必须在主线程传入）
     */
    explicit FMessageDispatchQueue(UGameplayMessageSubsystem* InOwner);

    FMessageDispatchQueue(const FMessageDispatchQueue&) = delete;
    FMessageDispatchQueue& operator=(const FMessageDispatchQueue&) = delete;
    FMessageDispatchQueue(FMessageDispatchQueue&&) = delete;
    FMessageDispatchQueue& operator=(FMessageDispatchQueue&&) = delete;

    /**
     * @brief 主线程重置队列运行态并排空上个关闭窗口的竞态残留任务（子系统 Initialize 时调用）
     */
    void ResetForInitialize();

    /**
     * @brief 关闭入队通道、封锁后续唤醒调度并排空丢弃队列残留任务（主线程 Deinitialize 时调用）
     */
    void Shutdown();

    /**
     * @brief 查询队列当前是否处于已关闭状态
     * @return 已关闭返回 true
     */
    [[nodiscard]] bool IsShuttingDown() const noexcept {
        return bIsShuttingDown.load(std::memory_order_acquire);
    }

    /**
     * @brief 线程安全压入一条待主线程执行的广播任务并合并投递 GameThread 唤醒
     * @param BroadcastTask 待执行任务闭包
     * @return 成功入队返回 Success，关闭态或空任务返回 Failure
     */
    TGameStatusResult<void> Enqueue(TUniqueFunction<void()> BroadcastTask);

    /**
     * @brief 在主线程立即排空并串行执行所有已入队广播任务（采用 seq_cst 防止弱内存序丢唤醒）
     * @return 本次实际执行的任务数量
     */
    int32 FlushPending();

private:
    TMpscQueue<TUniqueFunction<void()>> PendingQueue;
    const TWeakObjectPtr<UGameplayMessageSubsystem> CachedOwnerWeak;
    std::atomic<bool> bDrainScheduled{false};
    std::atomic<bool> bIsShuttingDown{false};
};

}  // namespace Game::Message::Private
