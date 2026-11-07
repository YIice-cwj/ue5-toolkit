/**
 * @file FTimeSlicedTaskHandle.h
 * @brief 主线程帧预算时间切片任务操作句柄
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UTimeSlicedTaskSubsystem;

/**
 * @brief 时间切片任务轻量控制句柄（可安全拷贝，支持主动取消与状态查询）
 */
class GAMECORE_API FTimeSlicedTaskHandle {
public:
    FTimeSlicedTaskHandle() = default;

    /**
     * @brief 取消关联的时间切片任务（已取消或已完成时幂等安全返回）
     */
    void Cancel();

    /**
     * @brief 查询关联任务是否仍处于排队或分帧执行中
     * @return 仍处于活跃态返回 true
     */
    [[nodiscard]] bool IsActive() const;

    /**
     * @brief 获取内部任务唯一递增 ID
     * @return 任务 ID（0 表示空句柄）
     */
    [[nodiscard]] uint64 GetTaskId() const noexcept {
        return TaskId;
    }

private:
    friend class UTimeSlicedTaskSubsystem;

    FTimeSlicedTaskHandle(UTimeSlicedTaskSubsystem* InSubsystem, uint64 InTaskId) noexcept;

private:
    TWeakObjectPtr<UTimeSlicedTaskSubsystem> SubsystemWeak;
    uint64 TaskId = 0ULL;
};
