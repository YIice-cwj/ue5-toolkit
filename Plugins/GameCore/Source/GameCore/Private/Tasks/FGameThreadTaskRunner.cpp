/**
 * @file FGameThreadTaskRunner.cpp
 * @brief 后台任务与回主线程续体统一执行工具实现
 */

#include "Tasks/FGameThreadTaskRunner.h"

#include "Async/Async.h"
#include "Tasks/Task.h"

/**
 * @brief 派发后台任务（工作线程执行）
 * @param Owner 宿主弱引用（已失效则提前短路不派发）
 * @param BackgroundWork 后台工作体（工作线程执行，禁止触碰 UObject）
 */
void FGameThreadTaskRunner::Run(TWeakObjectPtr<UObject> Owner, TFunction<void()> BackgroundWork) {
    if (!Owner.IsValid()) {
        return;
    }
    UE::Tasks::Launch(UE_SOURCE_LOCATION, MoveTemp(BackgroundWork));
}

/**
 * @brief 回主线程执行续体（宿主失效时静默丢弃，续体内解引用宿主安全）
 * @param Owner 宿主弱引用
 * @param GameThreadWork 续体（GameThread 执行，可安全操作 UObject）
 */
void FGameThreadTaskRunner::RunOnGameThread(TWeakObjectPtr<UObject> Owner, TFunction<void()> GameThreadWork) {
    AsyncTask(ENamedThreads::GameThread, [Owner, GameThreadWork = MoveTemp(GameThreadWork)]() {
        if (!Owner.IsValid()) {
            return;
        }
        GameThreadWork();
    });
}

void FGameThreadTaskRunner::RunWithContinuation(
    TWeakObjectPtr<UObject> Owner,
    TFunction<void()> BackgroundWork,
    TFunction<void()> GameThreadContinuation
) {
    Run(Owner, [Owner, BgWork = MoveTemp(BackgroundWork), Continuation = MoveTemp(GameThreadContinuation)]() mutable {
        BgWork();
        RunOnGameThread(Owner, MoveTemp(Continuation));
    });
}
