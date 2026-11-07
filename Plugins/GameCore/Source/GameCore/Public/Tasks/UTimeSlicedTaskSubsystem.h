/**
 * @file UTimeSlicedTaskSubsystem.h
 * @brief 主线程帧预算时间切片任务调度子系统（平摊大批量主线程工作，杜绝单帧卡顿）
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Tasks/FTimeSlicedTaskHandle.h"

#include "UTimeSlicedTaskSubsystem.generated.h"

/**
 * @brief 世界级主线程帧预算时间切片执行器（队列为空时自动休眠零 Tick 开销）
 */
UCLASS()
class GAMECORE_API UTimeSlicedTaskSubsystem : public UTickableWorldSubsystem {
    GENERATED_BODY()

public:
    using FStepTaskCallback = TFunction<bool()>;
    using FBatchIterationCallback = TFunction<void(int32 ItemIndex)>;
    using FBatchCompleteCallback = TFunction<void()>;

private:
    struct FTimedTaskEntry {
        uint64 TaskId = 0ULL;
        TWeakObjectPtr<UObject> Owner;
        FStepTaskCallback StepWork;
        bool bUseOwnerGuard = false;
    };

public:
    static constexpr double DEFAULT_FRAME_BUDGET_MS = 1.5;
    static constexpr double MIN_FRAME_BUDGET_MS = 0.05;

public:
    /**
     * @brief 世界销毁生命周期回调：清空全部待执行分片任务
     */
    void Deinitialize() override;

    /**
     * @brief 每帧在配置的时间预算内顺序推进切片任务（每帧非空时至少保底推进 1 步）
     * @param DeltaTime 帧流逝时间
     */
    void Tick(float DeltaTime) override;

    /**
     * @brief 统计性能组标识
     * @return Tickable 统计 ID
     */
    TStatId GetStatId() const override;

    /**
     * @brief 仅当存在待执行任务且非类默认对象时参与引擎 Tick
     * @return 需要 Tick 返回 true
     */
    bool IsTickable() const override;

    /**
     * @brief 设置单帧允许消耗的最大时间切片预算（毫秒，下限钳制 0.05ms）
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
     * @brief 注册绑定宿主弱引用的增量步进任务（回调返回 true 表示尚有后续步骤，宿主失效自动摘除）
     * @param Owner 宿主对象弱引用（入队前或分步前失效即自动丢弃）
     * @param StepWork 单步执行闭包（返回 true 继续保留下一轮，返回 false 宣告完成）
     * @return 任务控制句柄
     */
    FTimeSlicedTaskHandle EnqueueTask(TWeakObjectPtr<UObject> Owner, FStepTaskCallback StepWork);

    /**
     * @brief 注册无特定宿主绑定的全局世界级增量步进任务
     * @param StepWork 单步执行闭包（返回 true 继续保留，返回 false 完成）
     * @return 任务控制句柄
     */
    FTimeSlicedTaskHandle EnqueueGlobalTask(FStepTaskCallback StepWork);

    /**
     * @brief 注册绑定宿主弱引用的定长批量迭代任务（按帧预算逐项推进，全部完成后触发收尾回调）
     * @param Owner 宿主对象弱引用
     * @param TotalCount 迭代总项数（<=0 时若宿主有效立即执行 OnComplete 并返回空句柄）
     * @param PerItemWork 单项处理闭包（接收索引 [0, TotalCount-1]）
     * @param OnComplete 全部项完成后的可选收尾回调
     * @return 任务控制句柄
     */
    FTimeSlicedTaskHandle EnqueueBatch(
        TWeakObjectPtr<UObject> Owner,
        int32 TotalCount,
        FBatchIterationCallback PerItemWork,
        FBatchCompleteCallback OnComplete = nullptr
    );

    /**
     * @brief 按任务 ID 取消排队中或正在执行中的切片任务
     * @param TaskId 目标任务 ID
     * @return 找到并成功取消返回 true
     */
    bool CancelTask(uint64 TaskId);

    /**
     * @brief 查询指定任务 ID 当前是否仍在排队或执行中
     * @param TaskId 目标任务 ID
     * @return 仍活跃返回 true
     */
    [[nodiscard]] bool IsTaskActive(uint64 TaskId) const;

    /**
     * @brief 清空所有排队与正在执行的切片任务
     */
    void ClearAllTasks();

    /**
     * @brief 获取当前活跃切片任务总数
     * @return 队列与执行中任务总数
     */
    [[nodiscard]] int32 GetPendingTaskCount() const noexcept;

public:
    /**
     * @brief 获取时间切片子系统引用（上下文无效时触发断言）
     * @param WorldContextObject 世界上下文对象
     * @return 子系统引用
     */
    static UTimeSlicedTaskSubsystem& Get(const UObject* WorldContextObject);

    /**
     * @brief 安全获取时间切片子系统指针（上下文无效返回 nullptr）
     * @param WorldContextObject 世界上下文对象
     * @return 子系统指针
     */
    static UTimeSlicedTaskSubsystem* GetPtr(const UObject* WorldContextObject);

private:
    TArray<FTimedTaskEntry> TaskQueue;
    double FrameBudgetSeconds = DEFAULT_FRAME_BUDGET_MS * 0.001;
    uint64 NextTaskId = 0ULL;
    uint64 ExecutingTaskId = 0ULL;
    bool bExecutingTaskCancelled = false;
};
