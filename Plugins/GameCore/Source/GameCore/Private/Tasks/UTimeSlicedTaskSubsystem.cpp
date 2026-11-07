/**
 * @file UTimeSlicedTaskSubsystem.cpp
 * @brief UTimeSlicedTaskSubsystem 主线程帧预算时间切片任务调度子系统实现
 */

#include "Tasks/UTimeSlicedTaskSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

void UTimeSlicedTaskSubsystem::Deinitialize() {
    ClearAllTasks();
    Super::Deinitialize();
}

void UTimeSlicedTaskSubsystem::Tick([[maybe_unused]] float DeltaTime) {
    if (TaskQueue.IsEmpty()) {
        return;
    }

    const double StartSeconds = FPlatformTime::Seconds();
    const double DeadlineSeconds = StartSeconds + FrameBudgetSeconds;

    while (!TaskQueue.IsEmpty()) {
        FTimedTaskEntry CurrentEntry = MoveTemp(TaskQueue[0]);
        TaskQueue.RemoveAt(0, EAllowShrinking::No);

        if (CurrentEntry.bUseOwnerGuard && !CurrentEntry.Owner.IsValid()) {
            continue;
        }

        ExecutingTaskId = CurrentEntry.TaskId;
        bExecutingTaskCancelled = false;

        const bool bKeepRunning = CurrentEntry.StepWork ? CurrentEntry.StepWork() : false;
        const bool bWasCancelled = bExecutingTaskCancelled;
        ExecutingTaskId = 0ULL;
        bExecutingTaskCancelled = false;

        const bool bOwnerStillAlive = !CurrentEntry.bUseOwnerGuard || CurrentEntry.Owner.IsValid();
        if (bKeepRunning && !bWasCancelled && bOwnerStillAlive) {
            TaskQueue.Insert(MoveTemp(CurrentEntry), 0);
        }

        if (FPlatformTime::Seconds() >= DeadlineSeconds) {
            break;
        }
    }
}

TStatId UTimeSlicedTaskSubsystem::GetStatId() const {
    RETURN_QUICK_DECLARE_CYCLE_STAT(UTimeSlicedTaskSubsystem, STATGROUP_Tickables);
}

bool UTimeSlicedTaskSubsystem::IsTickable() const {
    return !IsTemplate() && !TaskQueue.IsEmpty();
}

void UTimeSlicedTaskSubsystem::SetFrameBudgetMilliseconds(double BudgetMilliseconds) noexcept {
    const double ClampedMs = FMath::Max(MIN_FRAME_BUDGET_MS, BudgetMilliseconds);
    FrameBudgetSeconds = ClampedMs * 0.001;
}

FTimeSlicedTaskHandle UTimeSlicedTaskSubsystem::EnqueueTask(TWeakObjectPtr<UObject> Owner, FStepTaskCallback StepWork) {
    if (!Owner.IsValid() || !StepWork) {
        return FTimeSlicedTaskHandle();
    }
    const uint64 Id = ++NextTaskId;
    TaskQueue.Add(FTimedTaskEntry{Id, Owner, MoveTemp(StepWork), true});
    return FTimeSlicedTaskHandle(this, Id);
}

FTimeSlicedTaskHandle UTimeSlicedTaskSubsystem::EnqueueGlobalTask(FStepTaskCallback StepWork) {
    if (!StepWork) {
        return FTimeSlicedTaskHandle();
    }
    const uint64 Id = ++NextTaskId;
    TaskQueue.Add(FTimedTaskEntry{Id, nullptr, MoveTemp(StepWork), false});
    return FTimeSlicedTaskHandle(this, Id);
}

FTimeSlicedTaskHandle UTimeSlicedTaskSubsystem::EnqueueBatch(
    TWeakObjectPtr<UObject> Owner,
    int32 TotalCount,
    FBatchIterationCallback PerItemWork,
    FBatchCompleteCallback OnComplete
) {
    if (!Owner.IsValid() || !PerItemWork) {
        return FTimeSlicedTaskHandle();
    }
    if (TotalCount <= 0) {
        if (OnComplete) {
            OnComplete();
        }
        return FTimeSlicedTaskHandle();
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

bool UTimeSlicedTaskSubsystem::CancelTask(uint64 TaskId) {
    if (TaskId == 0ULL) {
        return false;
    }
    if (ExecutingTaskId == TaskId) {
        bExecutingTaskCancelled = true;
        return true;
    }

    for (int32 Index = 0; Index < TaskQueue.Num(); ++Index) {
        if (TaskQueue[Index].TaskId == TaskId) {
            TaskQueue.RemoveAt(Index, EAllowShrinking::No);
            return true;
        }
    }
    return false;
}

bool UTimeSlicedTaskSubsystem::IsTaskActive(uint64 TaskId) const {
    if (TaskId == 0ULL) {
        return false;
    }
    if (ExecutingTaskId == TaskId && !bExecutingTaskCancelled) {
        return true;
    }
    for (const FTimedTaskEntry& Entry : TaskQueue) {
        if (Entry.TaskId == TaskId) {
            return true;
        }
    }
    return false;
}

void UTimeSlicedTaskSubsystem::ClearAllTasks() {
    TaskQueue.Empty();
    if (ExecutingTaskId != 0ULL) {
        bExecutingTaskCancelled = true;
    }
}

int32 UTimeSlicedTaskSubsystem::GetPendingTaskCount() const noexcept {
    const int32 ActiveRunning = (ExecutingTaskId != 0ULL && !bExecutingTaskCancelled) ? 1 : 0;
    return TaskQueue.Num() + ActiveRunning;
}

UTimeSlicedTaskSubsystem& UTimeSlicedTaskSubsystem::Get(const UObject* WorldContextObject) {
    UTimeSlicedTaskSubsystem* Subsystem = GetPtr(WorldContextObject);
    check(Subsystem != nullptr);
    return *Subsystem;
}

UTimeSlicedTaskSubsystem* UTimeSlicedTaskSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr || GEngine == nullptr) {
        return nullptr;
    }
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)) {
        return World->GetSubsystem<UTimeSlicedTaskSubsystem>();
    }
    return nullptr;
}
