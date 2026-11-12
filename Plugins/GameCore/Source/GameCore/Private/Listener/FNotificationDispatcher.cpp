/**
 * @file FNotificationDispatcher.cpp
 * @brief 全局通知 FIFO 调度器实现
 */

#include "Listener/FNotificationDispatcher.h"

/**
 * @brief 获取进程级唯一调度器
 * @return 调度器单例引用
 */
FNotificationDispatcher& FNotificationDispatcher::Get() {
    static FNotificationDispatcher Instance;
    return Instance;
}

/**
 * @brief 将通知追加到全局 FIFO 并选举唯一 drainer
 * @param InNotification 锁外执行的通知闭包
 * @return 当前线程当选为 drainer 返回 true，调用者释放源状态锁后必须调用 Drain
 */
bool FNotificationDispatcher::Enqueue(TFunction<void()> InNotification) {
    if (!InNotification) {
        return false;
    }
    FScopeLock Lock(&QueueMutex);
    Notifications.Enqueue(MoveTemp(InNotification));
    if (bDispatching) {
        return false;
    }
    bDispatching = true;
    return true;
}

/**
 * @brief 由当选 drainer 按 FIFO 顺序执行通知，重入与并发生产仅追加队列
 */
void FNotificationDispatcher::Drain() {
    struct FDrainGuard {
        FNotificationDispatcher* Self = nullptr;
        bool bCleanExit = false;

        ~FDrainGuard() {
            if (!bCleanExit && Self != nullptr) {
                FScopeLock Lock(&Self->QueueMutex);
                Self->bDispatching = false;
            }
        }
    } Guard{this};

    for (;;) {
        TFunction<void()> Notification;
        {
            FScopeLock Lock(&QueueMutex);
            if (!Notifications.Dequeue(Notification)) {
                bDispatching = false;
                Guard.bCleanExit = true;
                return;
            }
        }
        if (Notification) {
            Notification();
        }
    }
}
