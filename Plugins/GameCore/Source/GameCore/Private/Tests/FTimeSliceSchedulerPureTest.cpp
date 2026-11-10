/**
 * @file FTimeSliceSchedulerPureTest.cpp
 * @brief 验证纯 C++ 算法核心脱离 UWorld/引擎驱动在纯控制台/线程环境下独立运行
 */

#include "Async/FTimeSliceScheduler.h"
#include "Async/FThrottledTickScheduler.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTimeSliceSchedulerPureTest,
    "Game.Core.Tasks.TimeSliceScheduler.PureCppExecution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FTimeSliceScheduler 脱离 UWorld 独立步进与预算控制
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FTimeSliceSchedulerPureTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FTimeSliceScheduler Scheduler;
    TestTrue(TEXT("初始状态为空"), Scheduler.IsEmpty());
    TestEqual(TEXT("初始待处理任务数为 0"), Scheduler.GetPendingTaskCount(), 0);

    int32 StepCounter = 0;
    const uint64 TaskId = Scheduler.EnqueueTask([&StepCounter]() -> bool {
        ++StepCounter;
        const double EndTime = FPlatformTime::Seconds() + 0.0002;
        while (FPlatformTime::Seconds() < EndTime) {
        }
        return StepCounter < 3;
    });

    TestTrue(TEXT("任务成功入队且返回有效 ID"), TaskId > 0ULL);
    TestTrue(TEXT("任务处于活跃状态"), Scheduler.IsTaskActive(TaskId));
    TestFalse(TEXT("队列非空"), Scheduler.IsEmpty());

    Scheduler.Step(0.00005);
    TestEqual(TEXT("首轮推进后计数器为 1"), StepCounter, 1);
    TestTrue(TEXT("尚有后续步骤，任务继续活跃"), Scheduler.IsTaskActive(TaskId));

    Scheduler.Step(0.00005);
    TestEqual(TEXT("第二轮推进后计数器为 2"), StepCounter, 2);

    Scheduler.Step(0.00005);
    TestEqual(TEXT("第三轮推进后计数器为 3 宣告完成"), StepCounter, 3);
    TestFalse(TEXT("任务完成后不再活跃"), Scheduler.IsTaskActive(TaskId));
    TestTrue(TEXT("队列恢复为空"), Scheduler.IsEmpty());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FThrottledTickSchedulerPureTest,
    "Game.Core.Tasks.ThrottledTickScheduler.PureCppExecution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FThrottledTickScheduler 脱离 UWorld 独立频率分级与交错心跳推进
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FThrottledTickSchedulerPureTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FThrottledTickScheduler Scheduler;
    TestTrue(TEXT("初始状态为空"), Scheduler.IsEmpty());
    TestEqual(TEXT("初始注册数为 0"), Scheduler.GetRegisteredCount(), 0);

    int32 TickCount = 0;
    float LastDelta = 0.0f;
    const uint64 HandleId = Scheduler.RegisterIntervalTick(
        0.1f,
        [&TickCount, &LastDelta](float DeltaTime) {
            ++TickCount;
            LastDelta = DeltaTime;
        }
    );

    TestTrue(TEXT("心跳成功注册"), HandleId > 0ULL);
    TestTrue(TEXT("心跳任务活跃"), Scheduler.IsTickActive(HandleId));
    TestEqual(TEXT("当前注册数为 1"), Scheduler.GetRegisteredCount(), 1);

    Scheduler.Tick(0.02f);
    TestEqual(TEXT("流逝未达 0.1s 时未触发"), TickCount, 0);

    Scheduler.Tick(0.1f);
    TestEqual(TEXT("达到间隔后触发 1 次"), TickCount, 1);
    TestTrue(TEXT("回调接收实际累积流逝时间"), LastDelta >= 0.1f);

    TestTrue(TEXT("注销心跳成功"), Scheduler.UnregisterTick(HandleId));
    TestFalse(TEXT("注销后非活跃"), Scheduler.IsTickActive(HandleId));
    TestEqual(TEXT("注销后注册数归零"), Scheduler.GetRegisteredCount(), 0);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
