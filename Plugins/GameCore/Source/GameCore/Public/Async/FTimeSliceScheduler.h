/**
 * @file FTimeSliceScheduler.h
 * @brief 纯 C++ 主线程与无头环境帧预算时间切片任务调度器（机制与引擎外壳彻底分离）
 */

#pragma once

#include "CoreMinimal.h"

/**
 * @brief 帧预算时间切片调度器核心（零 UWorld 依赖，可在任何 GameThread 环境或主线程纯 C++ 单测中运行，非线程安全，调用方必须处于 GameThread）
 */
class GAMECORE_API FTimeSliceScheduler {
public:
    using FStepTaskCallback = TFunction<bool()>;
    using FOwnerValidator = TFunction<bool()>;
    using FBatchIterationCallback = TFunction<void(int32 ItemIndex)>;
    using FBatchCompleteCallback = TFunction<void()>;
    using FClockSource = TFunction<double()>;

private:
    struct FTimedTaskEntry {
        uint64 TaskId = 0ULL;
        FOwnerValidator OwnerValidator;
        FStepTaskCallback StepWork;
        bool bUseOwnerGuard = false;
    };

public:
    static constexpr double DEFAULT_FRAME_BUDGET_MS = 1.5;
    static constexpr double MIN_FRAME_BUDGET_MS = 0.05;

public:
    FTimeSliceScheduler() = default;
    ~FTimeSliceScheduler() = default;

    FTimeSliceScheduler(const FTimeSliceScheduler&) = delete;
    FTimeSliceScheduler& operator=(const FTimeSliceScheduler&) = delete;
    FTimeSliceScheduler(FTimeSliceScheduler&&) noexcept = default;
    FTimeSliceScheduler& operator=(FTimeSliceScheduler&&) noexcept = default;

    /**
     * @brief 推进调度器执行队列中的切片任务（协作式单步预算：每完成一个 StepWork 后检查预算截止时间，支持重入防护）
     * @param CustomBudgetSeconds 可选自定义预算秒数（<= 0 时采用内部设置的 FrameBudgetSeconds）
     */
    void Step(double CustomBudgetSeconds = -1.0);

    /**
     * @brief 设置单次步进允许消耗的最大时间切片预算（毫秒）
     * @param BudgetMilliseconds 单帧毫秒预算
     */
    void SetFrameBudgetMilliseconds(double BudgetMilliseconds) noexcept;

    /**
     * @brief 获取当前单帧最大时间切片预算（毫秒）
     * @return 单帧毫秒预算
     */
    [[nodiscard]] double GetFrameBudgetMilliseconds() const noexcept {
        return FrameBudgetSeconds * 1000.0;
    }

    /**
     * @brief 注入自定义秒级时钟源（供确定性回放或单元测试摆脱墙钟抖动，传入空闭包恢复默认 FPlatformTime::Seconds）
     * @param InClockSource 返回当前秒数的时钟函数
     */
    void SetClockSource(FClockSource InClockSource);

    /**
     * @brief 注册通用宿主生命周期校验守卫的增量切片任务
     * @param InValidator 宿主生命周期有效性断言闭包
     * @param StepWork 单步执行闭包（返回 true 继续保留下一轮，返回 false 完成）
     * @return 分配的任务 ID（0 代表入队失败）
     */
    uint64 EnqueueTask(FOwnerValidator InValidator, FStepTaskCallback StepWork);

    /**
     * @brief 针对 UE5 UObject 弱引用的重载入队接口
     * @param Owner 宿主对象弱引用
     * @param StepWork 单步执行闭包
     * @return 分配的任务 ID
     */
    uint64 EnqueueTask(TWeakObjectPtr<UObject> Owner, FStepTaskCallback StepWork);

    /**
     * @brief 注册无宿主绑定的全局切片任务
     * @param StepWork 单步执行闭包
     * @return 分配的任务 ID
     */
    uint64 EnqueueTask(FStepTaskCallback StepWork);

    /**
     * @brief 注册定长批量迭代切片任务
     * @param Owner 宿主对象弱引用
     * @param TotalCount 迭代总项数
     * @param PerItemWork 单项处理闭包
     * @param OnComplete 全部完成回调
     * @return 分配的任务 ID
     */
    uint64 EnqueueBatch(
        TWeakObjectPtr<UObject> Owner,
        int32 TotalCount,
        FBatchIterationCallback PerItemWork,
        FBatchCompleteCallback OnComplete = nullptr
    );

    /**
     * @brief 注册定长批量迭代切片任务（无宿主绑定的纯 C++ 全局任务）
     * @param TotalCount 迭代总项数
     * @param PerItemWork 单项处理闭包
     * @param OnComplete 全部完成回调
     * @return 分配的任务 ID
     */
    uint64 EnqueueBatch(
        int32 TotalCount,
        FBatchIterationCallback PerItemWork,
        FBatchCompleteCallback OnComplete = nullptr
    );

    /**
     * @brief 按任务 ID 取消任务
     * @param TaskId 目标任务 ID
     * @return 找到并成功取消返回 true
     */
    bool CancelTask(uint64 TaskId);

    /**
     * @brief 查询任务当前是否活跃
     * @param TaskId 目标任务 ID
     * @return 活跃态返回 true
     */
    [[nodiscard]] bool IsTaskActive(uint64 TaskId) const;

    /**
     * @brief 清空全部待执行任务并标记当前运行中任务取消
     */
    void ClearAllTasks();

    /**
     * @brief 查询待处理及执行中的任务总数
     * @return 任务数
     */
    [[nodiscard]] int32 GetPendingTaskCount() const noexcept;

    /**
     * @brief 队列是否为空
     * @return 为空返回 true
     */
    [[nodiscard]] bool IsEmpty() const noexcept {
        return (QueueHeadIndex >= TaskQueue.Num()) && ExecutingTaskId == 0ULL;
    }

private:
    /**
     * @brief 当已消费队头偏移量达到压缩阈值时原地搬移未消费区间以回收内存
     */
    void CompactTaskQueueIfNeeded();

    /**
     * @brief 获取当前秒级时间戳（优先使用注入时钟源，否则使用平台高精度秒时钟）
     * @return 当前时间戳（秒）
     */
    [[nodiscard]] double GetNowSeconds() const;

private:
    TArray<FTimedTaskEntry> TaskQueue;
    FClockSource CustomClock;
    double FrameBudgetSeconds = DEFAULT_FRAME_BUDGET_MS * 0.001;
    uint64 NextTaskId = 0ULL;
    uint64 ExecutingTaskId = 0ULL;
    int32 QueueHeadIndex = 0;
    bool bExecutingTaskCancelled = false;
    bool bIsStepping = false;
};
