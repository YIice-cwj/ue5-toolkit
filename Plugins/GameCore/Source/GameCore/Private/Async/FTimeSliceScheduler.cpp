/**
 * @file FTimeSliceScheduler.cpp
 * @brief 纯 C++ 帧预算时间切片任务调度器实现
 */

#include "Async/FTimeSliceScheduler.h"

#include "HAL/PlatformTime.h"
#include "Math/UnrealMathUtility.h"

/**
 * @brief 推进调度器执行队列中的切片任务（采用头游标 O(1) 出队，每步前后检查预算截止时间，支持重入防护）
 * @param CustomBudgetSeconds 可选自定义预算秒数（<= 0 时采用内部设置的 FrameBudgetSeconds）
 */
void FTimeSliceScheduler::Step(double CustomBudgetSeconds) {
    if (bIsStepping || QueueHeadIndex >= TaskQueue.Num()) {
        return;
    }

    bIsStepping = true;
    const double EffectiveBudget = (CustomBudgetSeconds > 0.0) ? CustomBudgetSeconds : FrameBudgetSeconds;
    const double StartSeconds = GetNowSeconds();
    const double DeadlineSeconds = StartSeconds + EffectiveBudget;

    while (QueueHeadIndex < TaskQueue.Num()) {
        FTimedTaskEntry CurrentEntry = MoveTemp(TaskQueue[QueueHeadIndex]);
        ++QueueHeadIndex;
        CompactTaskQueueIfNeeded();

        if (CurrentEntry.bUseOwnerGuard && CurrentEntry.OwnerValidator && !CurrentEntry.OwnerValidator()) {
            if (GetNowSeconds() >= DeadlineSeconds) {
                break;
            }
            continue;
        }

        ExecutingTaskId = CurrentEntry.TaskId;
        bExecutingTaskCancelled = false;

        const bool bKeepRunning = CurrentEntry.StepWork ? CurrentEntry.StepWork() : false;
        const bool bWasCancelled = bExecutingTaskCancelled;
        ExecutingTaskId = 0ULL;
        bExecutingTaskCancelled = false;

        const bool bOwnerStillAlive = !CurrentEntry.bUseOwnerGuard || (CurrentEntry.OwnerValidator && CurrentEntry.OwnerValidator());
        if (bKeepRunning && !bWasCancelled && bOwnerStillAlive) {
            TaskQueue.Add(MoveTemp(CurrentEntry));
        }

        if (GetNowSeconds() >= DeadlineSeconds) {
            break;
        }
    }

    CompactTaskQueueIfNeeded();
    bIsStepping = false;
}

/**
 * @brief 设置单次步进允许消耗的最大时间切片预算（毫秒）
 * @param BudgetMilliseconds 单帧毫秒预算
 */
void FTimeSliceScheduler::SetFrameBudgetMilliseconds(double BudgetMilliseconds) noexcept {
    const double ClampedMs = FMath::Max(MIN_FRAME_BUDGET_MS, BudgetMilliseconds);
    FrameBudgetSeconds = ClampedMs * 0.001;
}

/**
 * @brief 注入自定义秒级时钟源
 * @param InClockSource 返回当前秒数的时钟函数
 */
void FTimeSliceScheduler::SetClockSource(FClockSource InClockSource) {
    CustomClock = MoveTemp(InClockSource);
}

/**
 * @brief 注册通用宿主生命周期校验守卫的增量切片任务
 * @param InValidator 宿主生命周期有效性断言闭包
 * @param StepWork 单步执行闭包
 * @return 分配的任务 ID
 */
uint64 FTimeSliceScheduler::EnqueueTask(FOwnerValidator InValidator, FStepTaskCallback StepWork) {
    if (!InValidator || !InValidator() || !StepWork) {
        return 0ULL;
    }
    const uint64 Id = ++NextTaskId;
    TaskQueue.Add(FTimedTaskEntry{Id, MoveTemp(InValidator), MoveTemp(StepWork), true});
    return Id;
}

/**
 * @brief 针对 UE5 UObject 弱引用的重载入队接口
 * @param Owner 宿主对象弱引用
 * @param StepWork 单步执行闭包
 * @return 分配的任务 ID
 */
uint64 FTimeSliceScheduler::EnqueueTask(TWeakObjectPtr<UObject> Owner, FStepTaskCallback StepWork) {
    if (!Owner.IsValid() || !StepWork) {
        return 0ULL;
    }
    return EnqueueTask(
        [Owner]() -> bool {
            return Owner.IsValid();
        },
        MoveTemp(StepWork)
    );
}

/**
 * @brief 注册无宿主绑定的全局切片任务
 * @param StepWork 单步执行闭包
 * @return 分配的任务 ID
 */
uint64 FTimeSliceScheduler::EnqueueTask(FStepTaskCallback StepWork) {
    if (!StepWork) {
        return 0ULL;
    }
    const uint64 Id = ++NextTaskId;
    TaskQueue.Add(FTimedTaskEntry{Id, nullptr, MoveTemp(StepWork), false});
    return Id;
}

/**
 * @brief 注册定长批量迭代切片任务
 * @param Owner 宿主对象弱引用
 * @param TotalCount 迭代总项数
 * @param PerItemWork 单项处理闭包
 * @param OnComplete 全部完成回调
 * @return 分配的任务 ID
 */
uint64 FTimeSliceScheduler::EnqueueBatch(
    TWeakObjectPtr<UObject> Owner,
    int32 TotalCount,
    FBatchIterationCallback PerItemWork,
    FBatchCompleteCallback OnComplete) {
    if (!Owner.IsValid() || !PerItemWork) {
        return 0ULL;
    }
    if (TotalCount <= 0) {
        if (OnComplete) {
            OnComplete();
        }
        return 0ULL;
    }

    int32 CurrentIndex = 0;
    return EnqueueTask(
        Owner,
        [CurrentIndex, TotalCount, ItemFn = MoveTemp(PerItemWork), CompleteFn = MoveTemp(OnComplete)]() mutable -> bool {
            ItemFn(CurrentIndex);
            ++CurrentIndex;
            if (CurrentIndex < TotalCount) {
                return true;
            }
            if (CompleteFn) {
                CompleteFn();
            }
            return false;
        }
    );
}

/**
 * @brief 按任务 ID 取消任务
 * @param TaskId 目标任务 ID
 * @return 找到并成功取消返回 true
 */
bool FTimeSliceScheduler::CancelTask(uint64 TaskId) {
    if (TaskId == 0ULL) {
        return false;
    }
    if (ExecutingTaskId == TaskId) {
        bExecutingTaskCancelled = true;
        return true;
    }

    for (int32 Index = QueueHeadIndex; Index < TaskQueue.Num(); ++Index) {
        if (TaskQueue[Index].TaskId == TaskId) {
            if (Index == QueueHeadIndex) {
                TaskQueue[Index] = FTimedTaskEntry{};
                ++QueueHeadIndex;
                CompactTaskQueueIfNeeded();
            } else {
                TaskQueue.RemoveAt(Index, EAllowShrinking::No);
            }
            return true;
        }
    }
    return false;
}

/**
 * @brief 查询任务当前是否活跃
 * @param TaskId 目标任务 ID
 * @return 活跃态返回 true
 */
bool FTimeSliceScheduler::IsTaskActive(uint64 TaskId) const {
    if (TaskId == 0ULL) {
        return false;
    }
    if (ExecutingTaskId == TaskId && !bExecutingTaskCancelled) {
        return true;
    }
    for (int32 Index = QueueHeadIndex; Index < TaskQueue.Num(); ++Index) {
        if (TaskQueue[Index].TaskId == TaskId) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 清空全部待执行任务并标记当前运行中任务取消
 */
void FTimeSliceScheduler::ClearAllTasks() {
    TaskQueue.Empty();
    QueueHeadIndex = 0;
    if (ExecutingTaskId != 0ULL) {
        bExecutingTaskCancelled = true;
    }
}

/**
 * @brief 查询待处理及执行中的任务总数
 * @return 任务数
 */
int32 FTimeSliceScheduler::GetPendingTaskCount() const noexcept {
    const int32 ActiveRunning = (ExecutingTaskId != 0ULL && !bExecutingTaskCancelled) ? 1 : 0;
    const int32 QueuedCount = FMath::Max(0, TaskQueue.Num() - QueueHeadIndex);
    return QueuedCount + ActiveRunning;
}

/**
 * @brief 当已消费前缀达到阈值或队列已排空时批量回收头部空闲槽位，均摊保持 O(1) 出队复杂度
 */
void FTimeSliceScheduler::CompactTaskQueueIfNeeded() {
    if (QueueHeadIndex <= 0) {
        return;
    }
    if (QueueHeadIndex >= TaskQueue.Num()) {
        TaskQueue.Reset();
        QueueHeadIndex = 0;
    } else if (QueueHeadIndex >= 32 && QueueHeadIndex * 2 >= TaskQueue.Num()) {
        TaskQueue.RemoveAt(0, QueueHeadIndex, EAllowShrinking::No);
        QueueHeadIndex = 0;
    }
}

/**
 * @brief 获取当前秒级时间戳
 * @return 秒数
 */
double FTimeSliceScheduler::GetNowSeconds() const {
    return CustomClock ? CustomClock() : FPlatformTime::Seconds();
}
