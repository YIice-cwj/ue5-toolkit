/**
 * @file FNotificationDispatcher.cpp
 * @brief 全局通知 FIFO 调度器实现
 */

#include "Listener/FNotificationDispatcher.h"

FNotificationDispatcher& FNotificationDispatcher::Get() {
    static FNotificationDispatcher Instance;
    return Instance;
}

bool FNotificationDispatcher::Enqueue(TFunction<void()> InNotification) {
    FScopeLock Lock(&QueueMutex);
    Notifications.Enqueue(MoveTemp(InNotification));
    if (bDispatching) {
        return false;
    }
    bDispatching = true;
    return true;
}

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
        Notification();
    }
}
