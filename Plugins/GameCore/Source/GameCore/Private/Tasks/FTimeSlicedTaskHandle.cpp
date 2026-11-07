/**
 * @file FTimeSlicedTaskHandle.cpp
 * @brief FTimeSlicedTaskHandle 时间切片任务操作句柄实现
 */

#include "Tasks/FTimeSlicedTaskHandle.h"

#include "Tasks/UTimeSlicedTaskSubsystem.h"

void FTimeSlicedTaskHandle::Cancel() {
    if (TaskId == 0ULL) {
        return;
    }
    if (UTimeSlicedTaskSubsystem* Subsystem = SubsystemWeak.Get()) {
        Subsystem->CancelTask(TaskId);
    }
    TaskId = 0ULL;
    SubsystemWeak.Reset();
}

bool FTimeSlicedTaskHandle::IsActive() const {
    if (TaskId == 0ULL) {
        return false;
    }
    const UTimeSlicedTaskSubsystem* Subsystem = SubsystemWeak.Get();
    return Subsystem != nullptr && Subsystem->IsTaskActive(TaskId);
}

FTimeSlicedTaskHandle::FTimeSlicedTaskHandle(UTimeSlicedTaskSubsystem* InSubsystem, uint64 InTaskId) noexcept
    : SubsystemWeak(InSubsystem)
    , TaskId(InTaskId) {
}
