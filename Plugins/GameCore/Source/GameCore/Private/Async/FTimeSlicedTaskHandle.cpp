/**
 * @file FTimeSlicedTaskHandle.cpp
 * @brief FTimeSlicedTaskHandle 时间切片任务操作句柄实现
 */

#include "Async/FTimeSlicedTaskHandle.h"

#include "Async/UTimeSlicedTaskSubsystem.h"

/**
 * @brief 主动取消关联的时间切片任务并解绑所属子系统引用（幂等操作）
 */
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

/**
 * @brief 查询当前句柄所代表的任务是否仍处于活跃排队或执行态
 * @return 活跃态返回 true
 */
bool FTimeSlicedTaskHandle::IsActive() const {
    if (TaskId == 0ULL) {
        return false;
    }
    const UTimeSlicedTaskSubsystem* Subsystem = SubsystemWeak.Get();
    return Subsystem != nullptr && Subsystem->GetScheduler().IsTaskActive(TaskId);
}

/**
 * @brief 构造绑定子系统与任务唯一 ID 的时间切片任务操作句柄
 * @param InSubsystem 所属时间切片子系统指针
 * @param InTaskId 任务唯一分配 ID
 */
FTimeSlicedTaskHandle::FTimeSlicedTaskHandle(UTimeSlicedTaskSubsystem* InSubsystem, uint64 InTaskId) noexcept
    : SubsystemWeak(InSubsystem)
    , TaskId(InTaskId) {
}
