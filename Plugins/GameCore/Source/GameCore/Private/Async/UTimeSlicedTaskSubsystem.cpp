/**
 * @file UTimeSlicedTaskSubsystem.cpp
 * @brief UTimeSlicedTaskSubsystem 主线程帧预算时间切片任务调度子系统薄壳实现
 */

#include "Async/UTimeSlicedTaskSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

/**
 * @brief 世界注销生命周期回调：清空并中断全部挂起与执行中的切片任务
 */
void UTimeSlicedTaskSubsystem::Deinitialize() {
    ClearAllTasks();
    Super::Deinitialize();
}

/**
 * @brief 引擎每帧推进调度器执行时间切片任务（消耗当帧预设微秒/毫秒预算）
 * @param DeltaTime 帧间隔时间（当前切片调度器基于单步高精度时钟计时，不依赖此入参）
 */
void UTimeSlicedTaskSubsystem::Tick([[maybe_unused]] float DeltaTime) {
    Scheduler.Step();
}

/**
 * @brief 统计性能组标识
 * @return Tickable 统计 ID
 */
TStatId UTimeSlicedTaskSubsystem::GetStatId() const {
    RETURN_QUICK_DECLARE_CYCLE_STAT(UTimeSlicedTaskSubsystem, STATGROUP_Tickables);
}

/**
 * @brief 仅当存在待执行切片任务且非模板类时参与引擎主循环 Tick（零注册时零开销）
 * @return 需要 Tick 返回 true
 */
bool UTimeSlicedTaskSubsystem::IsTickable() const {
    return !IsTemplate() && !Scheduler.IsEmpty();
}

/**
 * @brief 动态调整单帧最大切片时间预算（毫秒）
 * @param BudgetMilliseconds 单帧允许消耗的最大毫秒数
 */
void UTimeSlicedTaskSubsystem::SetFrameBudgetMilliseconds(double BudgetMilliseconds) noexcept {
    Scheduler.SetFrameBudgetMilliseconds(BudgetMilliseconds);
}

/**
 * @brief 注册受 UObject 宿主弱引用生命周期保护的增量切片任务
 * @param Owner 宿主对象弱引用（宿主失效时任务自动丢弃）
 * @param StepWork 单步执行闭包（返回 true 继续保留下一轮，返回 false 表示完成）
 * @return 可操作句柄
 */
FTimeSlicedTaskHandle UTimeSlicedTaskSubsystem::EnqueueTask(TWeakObjectPtr<UObject> Owner, FStepTaskCallback StepWork) {
    const uint64 Id = Scheduler.EnqueueTask(Owner, MoveTemp(StepWork));
    return (Id != 0ULL) ? FTimeSlicedTaskHandle(this, Id) : FTimeSlicedTaskHandle();
}

/**
 * @brief 注册无特定宿主绑定的全局增量切片任务
 * @param StepWork 单步执行闭包（返回 true 继续保留下一轮，返回 false 表示完成）
 * @return 可操作句柄
 */
FTimeSlicedTaskHandle UTimeSlicedTaskSubsystem::EnqueueTask(FStepTaskCallback StepWork) {
    const uint64 Id = Scheduler.EnqueueTask(MoveTemp(StepWork));
    return (Id != 0ULL) ? FTimeSlicedTaskHandle(this, Id) : FTimeSlicedTaskHandle();
}

/**
 * @brief 注册定长批量迭代切片任务（自动平摊至多帧执行）
 * @param Owner 宿主对象弱引用
 * @param TotalCount 待处理的总元素数量
 * @param PerItemWork 单元素处理闭包（入参为当前元素索引）
 * @param OnComplete 全部批次执行完毕后的完成回调
 * @return 可操作句柄
 */
FTimeSlicedTaskHandle UTimeSlicedTaskSubsystem::EnqueueBatch(
    TWeakObjectPtr<UObject> Owner,
    int32 TotalCount,
    FBatchIterationCallback PerItemWork,
    FBatchCompleteCallback OnComplete) {
    const uint64 Id = Scheduler.EnqueueBatch(Owner, TotalCount, MoveTemp(PerItemWork), MoveTemp(OnComplete));
    return (Id != 0ULL) ? FTimeSlicedTaskHandle(this, Id) : FTimeSlicedTaskHandle();
}

/**
 * @brief 清空当前排队中的全部任务并标记当前运行中任务已取消
 */
void UTimeSlicedTaskSubsystem::ClearAllTasks() {
    Scheduler.ClearAllTasks();
}

/**
 * @brief 查询当前待处理及执行中的任务总数
 * @return 活跃任务数量
 */
int32 UTimeSlicedTaskSubsystem::GetPendingTaskCount() const noexcept {
    return Scheduler.GetPendingTaskCount();
}

/**
 * @brief 获取世界上下文绑定的时间切片子系统引用（断言必定非空）
 * @param WorldContextObject 世界上下文对象
 * @return 子系统实例引用
 */
UTimeSlicedTaskSubsystem& UTimeSlicedTaskSubsystem::Get(const UObject* WorldContextObject) {
    checkf(WorldContextObject != nullptr, TEXT("UTimeSlicedTaskSubsystem::Get requires a valid WorldContextObject"));
    UTimeSlicedTaskSubsystem* Subsystem = GetPtr(WorldContextObject);
    check(Subsystem != nullptr);
    return *Subsystem;
}

/**
 * @brief 安全获取世界上下文绑定的时间切片子系统指针（空指针保护）
 * @param WorldContextObject 世界上下文对象
 * @return 子系统实例指针，获取失败返回 nullptr
 */
UTimeSlicedTaskSubsystem* UTimeSlicedTaskSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr || GEngine == nullptr) {
        return nullptr;
    }
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)) {
        return World->GetSubsystem<UTimeSlicedTaskSubsystem>();
    }
    return nullptr;
}
