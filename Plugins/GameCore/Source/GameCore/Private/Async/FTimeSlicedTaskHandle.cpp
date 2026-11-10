/**
 * @file FTimeSlicedTaskHandle.cpp
 * @brief FTimeSlicedTaskHandle 时间切片任务操作句柄实现
 */

#include "Async/FTimeSlicedTaskHandle.h"

#include "Async/UTimeSlicedTaskSubsystem.h"

void FTimeSlicedTaskHandle::Cancel() {
    if (TaskId == 0ULL) {
        return;
    }
    if (UTimeSlicedTaskSubsystem* Subsystem = SubsystemWeak.Get()) {
        Subsystem->GetScheduler().CancelTask(TaskId);
    }
    TaskId = 0ULL;
    SubsystemWeak.Reset();
}

bool FTimeSlicedTaskHandle::IsActive() const {
    if (TaskId == 0ULL) {
        return false;
    }
    const UTimeSlicedTaskSubsystem* Subsystem = SubsystemWeak.Get();
    return Subsystem != nullptr && Subsystem->GetScheduler().IsTaskActive(TaskId);
}

FTimeSlicedTaskHandle::FTimeSlicedTaskHandle(UTimeSlicedTaskSubsystem* InSubsystem, uint64 InTaskId) noexcept
    : SubsystemWeak(InSubsystem)
    , TaskId(InTaskId) {
}
