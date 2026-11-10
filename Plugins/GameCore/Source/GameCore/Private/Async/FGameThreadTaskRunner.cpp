/**
 * @file FGameThreadTaskRunner.cpp
 * @brief 后台任务与回主线程续体统一执行工具实现
 */

#include "Async/FGameThreadTaskRunner.h"

#include "Async/Async.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"

#include <atomic>

namespace {

std::atomic<int32> GInFlightBackgroundTasks{0};
std::atomic<uint64> GTaskCancelEpoch{1ULL};
FCriticalSection GActiveTasksLock;
TArray<UE::Tasks::FTask> GActiveTasks;

/**
 * @brief 在飞任务计数 RAII 守卫
 */
struct FScopedInFlightTaskTracker {
    FScopedInFlightTaskTracker() noexcept = default;

    ~FScopedInFlightTaskTracker() noexcept {
        GInFlightBackgroundTasks.fetch_sub(1, std::memory_order_acq_rel);
    }

    FScopedInFlightTaskTracker(const FScopedInFlightTaskTracker&) = delete;
    FScopedInFlightTaskTracker& operator=(const FScopedInFlightTaskTracker&) = delete;
};

/**
 * @brief 注册底层任务句柄并顺带清理已完成的历史句柄
 * @param InTask 新派发的底层任务句柄
 */
void TrackActiveTask(const UE::Tasks::FTask& InTask) {
    FScopeLock Lock(&GActiveTasksLock);
    GActiveTasks.RemoveAllSwap([](const UE::Tasks::FTask& Existing) {
        return Existing.IsCompleted();
    }, EAllowShrinking::No);
    GActiveTasks.Add(InTask);
}

} // namespace

/**
 * @brief 构造绑定底层任务与取消状态的异步任务句柄
 * @param InTask 底层 UE Tasks 句柄
 * @param InCancelState 任务级协同取消共享状态
 */
FGameAsyncTaskHandle::FGameAsyncTaskHandle(
    UE::Tasks::FTask InTask,
    TSharedPtr<FCancellationState> InCancelState
) noexcept
    : Task(MoveTemp(InTask))
    , CancelState(MoveTemp(InCancelState)) {
}

/**
 * @brief 查询句柄是否绑定了有效后台任务
 * @return 有效返回 true
 */
bool FGameAsyncTaskHandle::IsValid() const noexcept {
    return Task.IsValid();
}

/**
 * @brief 查询后台任务是否已执行完成
 * @return 已完成或未绑定有效任务返回 true
 */
bool FGameAsyncTaskHandle::IsCompleted() const noexcept {
    return !Task.IsValid() || Task.IsCompleted();
}

/**
 * @brief 向当前后台任务发送协同取消请求
 * @return 首次成功触发取消返回 true
 */
bool FGameAsyncTaskHandle::Cancel() {
    if (CancelState.IsValid()) {
        return CancelState->RequestCancel();
    }
    return false;
}

/**
 * @brief 等待后台任务执行完毕（参与 UE TaskGraph 工作窃取回撤，零忙等自旋）
 */
void FGameAsyncTaskHandle::Wait() const {
    if (Task.IsValid()) {
        Task.Wait();
    }
}

/**
 * @brief 带超时等待后台任务执行完毕
 * @param Timeout 最大等待时长
 * @return 超时前任务完成返回 true
 */
bool FGameAsyncTaskHandle::WaitWithTimeout(FTimespan Timeout) const {
    if (!Task.IsValid()) {
        return true;
    }
    return Task.Wait(Timeout);
}

/**
 * @brief 请求取消并等待当前后台任务退出（Stop 语义）
 * @param Timeout 最大等待时长
 * @return 超时前任务完成返回 true
 */
bool FGameAsyncTaskHandle::Stop(FTimespan Timeout) {
    Cancel();
    return WaitWithTimeout(Timeout);
}

/**
 * @brief 派发后台任务（工作线程执行，自动追踪在飞计数，工作线程启动时再次校验宿主存活）
 * @param Owner 宿主弱引用（已失效则提前短路不派发）
 * @param BackgroundWork 后台工作体（工作线程执行，禁止触碰 UObject）
 * @return 异步任务控制句柄
 */
FGameAsyncTaskHandle FGameThreadTaskRunner::Run(TWeakObjectPtr<UObject> Owner, TFunction<void()> BackgroundWork) {
    return Run(MoveTemp(Owner), FCancellationToken::None(), MoveTemp(BackgroundWork));
}

/**
 * @brief 派发支持协同取消令牌的后台任务（工作线程启动前自动进行线程安全的宿主存活校验与取消世代校验）
 * @param Owner 宿主弱引用（已失效则提前短路不派发）
 * @param CancellationToken 协同取消令牌（已取消则提前短路不执行）
 * @param BackgroundWork 后台工作体
 * @return 异步任务控制句柄
 */
FGameAsyncTaskHandle FGameThreadTaskRunner::Run(
    TWeakObjectPtr<UObject> Owner,
    FCancellationToken CancellationToken,
    TFunction<void()> BackgroundWork
) {
    if (!Owner.IsValid(false, true) || !BackgroundWork || CancellationToken.IsCancellationRequested()) {
        return FGameAsyncTaskHandle();
    }

    const uint64 DispatchEpoch = GTaskCancelEpoch.load(std::memory_order_acquire);
    TSharedPtr<FCancellationState> TaskCancelState = MakeShared<FCancellationState>();
    GInFlightBackgroundTasks.fetch_add(1, std::memory_order_acq_rel);

    UE::Tasks::FTask LaunchedTask = UE::Tasks::Launch(
        UE_SOURCE_LOCATION,
        [Owner, DispatchEpoch, TaskCancelState, Token = MoveTemp(CancellationToken), Work = MoveTemp(BackgroundWork)]() mutable {
            const FScopedInFlightTaskTracker Tracker;
            if (GTaskCancelEpoch.load(std::memory_order_acquire) != DispatchEpoch) {
                return;
            }
            if (TaskCancelState->IsCancellationRequested() || Token.IsCancellationRequested() || !Owner.IsValid(false, true)) {
                return;
            }
            Work();
        }
    );

    TrackActiveTask(LaunchedTask);
    return FGameAsyncTaskHandle(MoveTemp(LaunchedTask), MoveTemp(TaskCancelState));
}

/**
 * @brief 派发无特定 UObject 宿主绑定的全局后台任务（支持协同取消令牌与在飞计数追踪）
 * @param CancellationToken 协同取消令牌
 * @param BackgroundWork 后台工作体（工作线程执行，禁止触碰 UObject）
 * @return 异步任务控制句柄
 */
FGameAsyncTaskHandle FGameThreadTaskRunner::Run(
    FCancellationToken CancellationToken,
    TFunction<void()> BackgroundWork
) {
    if (!BackgroundWork || CancellationToken.IsCancellationRequested()) {
        return FGameAsyncTaskHandle();
    }

    const uint64 DispatchEpoch = GTaskCancelEpoch.load(std::memory_order_acquire);
    TSharedPtr<FCancellationState> TaskCancelState = MakeShared<FCancellationState>();
    GInFlightBackgroundTasks.fetch_add(1, std::memory_order_acq_rel);

    UE::Tasks::FTask LaunchedTask = UE::Tasks::Launch(
        UE_SOURCE_LOCATION,
        [DispatchEpoch, TaskCancelState, Token = MoveTemp(CancellationToken), Work = MoveTemp(BackgroundWork)]() mutable {
            const FScopedInFlightTaskTracker Tracker;
            if (GTaskCancelEpoch.load(std::memory_order_acquire) != DispatchEpoch) {
                return;
            }
            if (TaskCancelState->IsCancellationRequested() || Token.IsCancellationRequested()) {
                return;
            }
            Work();
        }
    );

    TrackActiveTask(LaunchedTask);
    return FGameAsyncTaskHandle(MoveTemp(LaunchedTask), MoveTemp(TaskCancelState));
}

/**
 * @brief 回主线程执行续体（宿主失效时静默丢弃，续体内解引用宿主安全）
 * @param Owner 宿主弱引用
 * @param GameThreadWork 续体（GameThread 执行，可安全操作 UObject）
 */
void FGameThreadTaskRunner::RunOnGameThread(TWeakObjectPtr<UObject> Owner, TFunction<void()> GameThreadWork) {
    if (!Owner.IsValid(false, true) || !GameThreadWork) {
        return;
    }
    const uint64 DispatchEpoch = GTaskCancelEpoch.load(std::memory_order_acquire);
    AsyncTask(ENamedThreads::GameThread, [Owner, DispatchEpoch, GameThreadWork = MoveTemp(GameThreadWork)]() {
        if (GTaskCancelEpoch.load(std::memory_order_acquire) != DispatchEpoch || !Owner.IsValid()) {
            return;
        }
        GameThreadWork();
    });
}

/**
 * @brief 投递无特定 UObject 宿主绑定的全局续体至主线程执行（受全局取消世代保护）
 * @param GameThreadWork 主线程工作体
 */
void FGameThreadTaskRunner::RunOnGameThread(TFunction<void()> GameThreadWork) {
    if (!GameThreadWork) {
        return;
    }
    const uint64 DispatchEpoch = GTaskCancelEpoch.load(std::memory_order_acquire);
    AsyncTask(ENamedThreads::GameThread, [DispatchEpoch, GameThreadWork = MoveTemp(GameThreadWork)]() {
        if (GTaskCancelEpoch.load(std::memory_order_acquire) != DispatchEpoch) {
            return;
        }
        GameThreadWork();
    });
}

/**
 * @brief 请求全局取消所有已排队但尚未开始执行（或正在等待投递回主线程）的后台任务
 */
void FGameThreadTaskRunner::CancelAllPendingTasks() noexcept {
    GTaskCancelEpoch.fetch_add(1ULL, std::memory_order_acq_rel);
}

/**
 * @brief 查询当前正在后台工作线程中执行的在飞任务数量
 * @return 在飞后台任务计数
 */
int32 FGameThreadTaskRunner::GetInFlightTaskCount() noexcept {
    return GInFlightBackgroundTasks.load(std::memory_order_acquire);
}

/**
 * @brief 等待所有已派发的后台在飞任务执行完毕（优先通过 UE::Tasks::FTask::Wait 协同等待，供模块关停或单元测试排空使用）
 * @param TimeoutSeconds 最大等待超时秒数
 * @return 超时前全部在飞任务排空返回 true
 */
bool FGameThreadTaskRunner::FlushInFlightTasks(double TimeoutSeconds) {
    const double ClampedTimeout = FMath::Max(0.0, TimeoutSeconds);
    const double Deadline = FPlatformTime::Seconds() + ClampedTimeout;

    TArray<UE::Tasks::FTask> Snapshot;
    {
        FScopeLock Lock(&GActiveTasksLock);
        Snapshot = GActiveTasks;
    }

    for (const UE::Tasks::FTask& Task : Snapshot) {
        if (Task.IsValid() && !Task.IsCompleted()) {
            const double Remaining = FMath::Max(0.0, Deadline - FPlatformTime::Seconds());
            Task.Wait(FTimespan::FromSeconds(Remaining));
        }
    }

    while (GInFlightBackgroundTasks.load(std::memory_order_acquire) > 0) {
        if (FPlatformTime::Seconds() >= Deadline) {
            return GInFlightBackgroundTasks.load(std::memory_order_acquire) == 0;
        }
        FPlatformProcess::Yield();
    }

    {
        FScopeLock Lock(&GActiveTasksLock);
        GActiveTasks.RemoveAllSwap([](const UE::Tasks::FTask& Existing) {
            return Existing.IsCompleted();
        }, EAllowShrinking::No);
    }
    return true;
}
