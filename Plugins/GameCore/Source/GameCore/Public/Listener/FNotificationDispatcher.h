/**
 * @file FNotificationDispatcher.h
 * @brief 全局通知 FIFO 调度器（唯一 drainer，回调与源状态锁解耦）
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "HAL/CriticalSection.h"
#include "Templates/Function.h"

/**
 * @brief 串行化 Config 与值观察者通知的全局调度器
 */
class GAMECORE_API FNotificationDispatcher {
public:
    /**
     * @brief 获取进程级唯一调度器
     * @return 调度器单例引用
     */
    static FNotificationDispatcher& Get();

    /**
     * @brief 将通知追加到全局 FIFO 并选举唯一 drainer
     * @param InNotification 锁外执行的通知闭包
     * @return 当前线程当选为 drainer 返回 true，调用者释放源状态锁后必须调用 Drain
     */
    [[nodiscard]] bool Enqueue(TFunction<void()> InNotification);

    /**
     * @brief 由当选 drainer 按 FIFO 顺序执行通知，重入与并发生产仅追加队列
     */
    void Drain();

private:
    FNotificationDispatcher() = default;

private:
    FCriticalSection QueueMutex;
    TQueue<TFunction<void()>> Notifications;
    bool bDispatching = false;
};
