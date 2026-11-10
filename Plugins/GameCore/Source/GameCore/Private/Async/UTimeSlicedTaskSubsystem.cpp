/**
 * @file UTimeSlicedTaskSubsystem.cpp
 * @brief UTimeSlicedTaskSubsystem 主线程帧预算时间切片任务调度子系统薄壳实现
 */

#include "Async/UTimeSlicedTaskSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

void UTimeSlicedTaskSubsystem::Deinitialize() {
    ClearAllTasks();
    Super::Deinitialize();
}

void UTimeSlicedTaskSubsystem::Tick([[maybe_unused]] float DeltaTime) {
    Scheduler.Step();
}

TStatId UTimeSlicedTaskSubsystem::GetStatId() const {
    RETURN_QUICK_DECLARE_CYCLE_STAT(UTimeSlicedTaskSubsystem, STATGROUP_Tickables);
}

bool UTimeSlicedTaskSubsystem::IsTickable() const {
    return !IsTemplate() && !Scheduler.IsEmpty();
}

void UTimeSlicedTaskSubsystem::SetFrameBudgetMilliseconds(double BudgetMilliseconds) noexcept {
    Scheduler.SetFrameBudgetMilliseconds(BudgetMilliseconds);
}

FTimeSlicedTaskHandle UTimeSlicedTaskSubsystem::EnqueueTask(TWeakObjectPtr<UObject> Owner, FStepTaskCallback StepWork) {
    const uint64 Id = Scheduler.EnqueueTask(Owner, MoveTemp(StepWork));
    return (Id != 0ULL) ? FTimeSlicedTaskHandle(this, Id) : FTimeSlicedTaskHandle();
}

FTimeSlicedTaskHandle UTimeSlicedTaskSubsystem::EnqueueTask(FStepTaskCallback StepWork) {
    const uint64 Id = Scheduler.EnqueueTask(MoveTemp(StepWork));
    return (Id != 0ULL) ? FTimeSlicedTaskHandle(this, Id) : FTimeSlicedTaskHandle();
}

FTimeSlicedTaskHandle UTimeSlicedTaskSubsystem::EnqueueBatch(
    TWeakObjectPtr<UObject> Owner,
    int32 TotalCount,
    FBatchIterationCallback PerItemWork,
    FBatchCompleteCallback OnComplete) {
    const uint64 Id = Scheduler.EnqueueBatch(Owner, TotalCount, MoveTemp(PerItemWork), MoveTemp(OnComplete));
    return (Id != 0ULL) ? FTimeSlicedTaskHandle(this, Id) : FTimeSlicedTaskHandle();
}

void UTimeSlicedTaskSubsystem::ClearAllTasks() {
    Scheduler.ClearAllTasks();
}

int32 UTimeSlicedTaskSubsystem::GetPendingTaskCount() const noexcept {
    return Scheduler.GetPendingTaskCount();
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
