/**
 * @file UTimeSlicedTaskSubsystemTest.cpp
 * @brief FTimeSlicedTaskHandle 与 UTimeSlicedTaskSubsystem 主线程时间切片调度单元测试
 */

#include "Async/UTimeSlicedTaskSubsystem.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

namespace {

UWorld* FindTestWorldForTimeSlice() {
    if (GEngine == nullptr) {
        return nullptr;
    }
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World() != nullptr) {
            return Context.World();
        }
    }
    return nullptr;
}

void BurnCpuSeconds(double DurationSeconds) {
    const double EndTime = FPlatformTime::Seconds() + DurationSeconds;
    while (FPlatformTime::Seconds() < EndTime) {
    }
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTimeSlicedSubsystemBudgetAndBatchTest,
    "Game.Core.Async.Tasks.TimeSliced.BudgetAndBatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证帧预算截断分帧推进、批量任务逐项完成回调与空队列休眠状态
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FTimeSlicedSubsystemBudgetAndBatchTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TestNull(TEXT("空上下文 GetPtr 返回 nullptr"), UTimeSlicedTaskSubsystem::GetPtr(nullptr));

    FTimeSlicedTaskHandle EmptyHandle;
    TestFalse(TEXT("默认空句柄非活跃"), EmptyHandle.IsActive());
    TestEqual(TEXT("默认空句柄 ID 为 0"), EmptyHandle.GetTaskId(), 0ULL);
    EmptyHandle.Cancel();

    UWorld* World = FindTestWorldForTimeSlice();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        return false;
    }

    UTimeSlicedTaskSubsystem& Subsystem = UTimeSlicedTaskSubsystem::Get(World);
    Subsystem.ClearAllTasks();
    TestFalse(TEXT("空队列时 IsTickable 返回 false 保持零 Tick 开销"), Subsystem.IsTickable());
    TestEqual(TEXT("空队列待执行数为 0"), Subsystem.GetPendingTaskCount(), 0);
    Subsystem.Tick(0.016f);

    Subsystem.SetFrameBudgetMilliseconds(0.001);
    TestTrue(
        TEXT("极小帧预算自动钳制到 MIN_FRAME_BUDGET_MS"),
        FMath::IsNearlyEqual(Subsystem.GetFrameBudgetMilliseconds(), FTimeSliceScheduler::MIN_FRAME_BUDGET_MS, 1e-4)
    );

    UGameInstance* Owner = NewObject<UGameInstance>();
    bool bZeroBatchCompleted = false;
    const FTimeSlicedTaskHandle ZeroHandle = Subsystem.EnqueueBatch(
        Owner,
        0,
        [](int32) {},
        [&bZeroBatchCompleted]() { bZeroBatchCompleted = true; }
    );
    TestFalse(TEXT("零长度批次同步收敛不保留活跃句柄"), ZeroHandle.IsActive());
    TestTrue(TEXT("零长度批次立即触发 OnComplete"), bZeroBatchCompleted);

    TArray<int32> ProcessedIndices;
    bool bBatchFinished = false;
    FTimeSlicedTaskHandle BatchHandle = Subsystem.EnqueueBatch(
        Owner,
        4,
        [&ProcessedIndices](int32 ItemIndex) {
            ProcessedIndices.Add(ItemIndex);
            BurnCpuSeconds(0.0002);
        },
        [&bBatchFinished]() {
            bBatchFinished = true;
        }
    );

    TestTrue(TEXT("批量任务入队后句柄处于活跃态"), BatchHandle.IsActive());
    TestTrue(TEXT("非空队列 IsTickable 为真"), Subsystem.IsTickable());

    Subsystem.Tick(0.016f);
    TestEqual(TEXT("首帧因超出 0.05ms 预算精确只执行 1 项即让出主线程"), ProcessedIndices.Num(), 1);
    TestFalse(TEXT("未执行完所有项前不提前触发 OnComplete"), bBatchFinished);

    for (int32 TickCount = 0; TickCount < 5 && BatchHandle.IsActive(); ++TickCount) {
        Subsystem.Tick(0.016f);
    }

    TestFalse(TEXT("全部 4 项推进完成后句柄自动变为非活跃"), BatchHandle.IsActive());
    TestTrue(TEXT("全部 4 项完成后精确触发 OnComplete"), bBatchFinished);
    TestEqual(TEXT("累计处理项数为 4"), ProcessedIndices.Num(), 4);
    if (ProcessedIndices.Num() == 4) {
        TestEqual(TEXT("顺序索引 0"), ProcessedIndices[0], 0);
        TestEqual(TEXT("顺序索引 1"), ProcessedIndices[1], 1);
        TestEqual(TEXT("顺序索引 2"), ProcessedIndices[2], 2);
        TestEqual(TEXT("顺序索引 3"), ProcessedIndices[3], 3);
    }

    Subsystem.SetFrameBudgetMilliseconds(FTimeSliceScheduler::DEFAULT_FRAME_BUDGET_MS);
    Owner->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTimeSlicedSubsystemWeakOwnerAndCancelTest,
    "Game.Core.Async.Tasks.TimeSliced.WeakOwnerAndCancel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证宿主弱引用销毁自动摘除、外部取消与步进闭包内部自取消重入安全性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FTimeSlicedSubsystemWeakOwnerAndCancelTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = FindTestWorldForTimeSlice();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        return false;
    }

    UTimeSlicedTaskSubsystem& Subsystem = UTimeSlicedTaskSubsystem::Get(World);
    Subsystem.ClearAllTasks();
    Subsystem.SetFrameBudgetMilliseconds(5.0);

    TestFalse(TEXT("失效宿主入队返回空句柄"), Subsystem.EnqueueTask(nullptr, []() { return false; }).IsActive());
    TestFalse(TEXT("空回调全局任务入队返回空句柄"), Subsystem.EnqueueTask(nullptr).IsActive());
    TestFalse(TEXT("取消无效 ID 0 返回 false"), Subsystem.GetScheduler().CancelTask(0ULL));
    TestFalse(TEXT("取消不存在 ID 返回 false"), Subsystem.GetScheduler().CancelTask(999999ULL));

    UGameInstance* DyingOwner = NewObject<UGameInstance>();
    bool bDyingTaskInvoked = false;
    const FTimeSlicedTaskHandle DyingHandle = Subsystem.EnqueueTask(DyingOwner, [&bDyingTaskInvoked]() {
        bDyingTaskInvoked = true;
        return false;
    });
    TestTrue(TEXT("宿主销毁前任务在队列中"), DyingHandle.IsActive());
    DyingOwner->MarkAsGarbage();

    Subsystem.Tick(0.016f);
    TestFalse(TEXT("宿主已标记垃圾回收后任务自动跳过绝不回调"), bDyingTaskInvoked);
    TestEqual(TEXT("跳过失效宿主后队列清空"), Subsystem.GetPendingTaskCount(), 0);

    int32 GlobalSteps = 0;
    FTimeSlicedTaskHandle CancelableGlobal = Subsystem.EnqueueTask([&GlobalSteps]() {
        ++GlobalSteps;
        return true;
    });
    CancelableGlobal.Cancel();
    TestFalse(TEXT("主动 Cancel 后句柄失活"), CancelableGlobal.IsActive());
    Subsystem.Tick(0.016f);
    TestEqual(TEXT("已取消任务绝不执行"), GlobalSteps, 0);

    int32 SelfCancelSteps = 0;
    FTimeSlicedTaskHandle SelfCancelHandle;
    SelfCancelHandle = Subsystem.EnqueueTask([this, &SelfCancelSteps, &SelfCancelHandle, &Subsystem]() {
        ++SelfCancelSteps;
        TestEqual(TEXT("执行期间活跃计数包含当前运行项"), Subsystem.GetPendingTaskCount(), 1);
        SelfCancelHandle.Cancel();
        return true;
    });
    Subsystem.Tick(0.016f);
    Subsystem.Tick(0.016f);
    TestEqual(TEXT("闭包内部自取消后即使返回 true 也不会再次重入队列"), SelfCancelSteps, 1);
    TestEqual(TEXT("自取消后队列完全清空"), Subsystem.GetPendingTaskCount(), 0);

    return true;
}

#endif
