/**
 * @file FGameThreadTaskRunnerTest.cpp
 * @brief 异步任务调度与主线程续体工具自动化测试，Shipping 构建自动剔除
 */

#include "Async/FGameThreadTaskRunner.h"

#if WITH_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameTaskRunnerBasicContinuationTest, "Game.Core.Async.Tasks.TaskRunner.BasicContinuation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证异步工作线程计算与主线程续体结果投递
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 */
bool FGameTaskRunnerBasicContinuationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* Owner = NewObject<UGameInstance>();
    TestNotNull(TEXT("测试宿主对象有效"), Owner);
    if (!Owner) {
        return false;
    }

    FEvent* ContinuationDoneEvent = FPlatformProcess::GetSynchEventFromPool(false);
    std::atomic<bool> bContinuationExecutedOnGameThread{false};
    std::atomic<int32> ReceivedResult{0};

    FGameThreadTaskRunner::Run(
        Owner,
        [Owner, ContinuationDoneEvent, &bContinuationExecutedOnGameThread, &ReceivedResult]() {
            const int32 Result = 42;
            FGameThreadTaskRunner::RunOnGameThread(
                Owner,
                [ContinuationDoneEvent, &bContinuationExecutedOnGameThread, &ReceivedResult, Result]() {
                    bContinuationExecutedOnGameThread.store(IsInGameThread());
                    ReceivedResult.store(Result);
                    ContinuationDoneEvent->Trigger();
                }
            );
        }
    );

    bool bDone = false;
    for (int32 Iter = 0; Iter < 1000; ++Iter) {
        FPlatformProcess::Yield();
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        if (ContinuationDoneEvent->Wait(5)) {
            bDone = true;
            break;
        }
    }

    TestTrue(TEXT("续体在合理时间内执行完毕"), bDone);
    TestTrue(TEXT("续体必须运行在 GameThread 主线程"), bContinuationExecutedOnGameThread.load());
    TestEqual(TEXT("续体接收的后台计算结果正确"), ReceivedResult.load(), 42);

    FPlatformProcess::ReturnSynchEventToPool(ContinuationDoneEvent);
    Owner->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameTaskRunnerVoidContinuationTest, "Game.Core.Async.Tasks.TaskRunner.VoidContinuation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证无返回值后台任务与主线程续体执行流程
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 */
bool FGameTaskRunnerVoidContinuationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* Owner = NewObject<UGameInstance>();
    TestNotNull(TEXT("测试宿主对象有效"), Owner);
    if (!Owner) {
        return false;
    }

    FEvent* ContinuationDoneEvent = FPlatformProcess::GetSynchEventFromPool(false);
    std::atomic<bool> bBackgroundRan{false};
    std::atomic<bool> bContinuationRanOnGameThread{false};

    FGameThreadTaskRunner::Run(
        FCancellationToken::None(),
        [ContinuationDoneEvent, &bBackgroundRan, &bContinuationRanOnGameThread]() {
            bBackgroundRan.store(true);
            FGameThreadTaskRunner::RunOnGameThread([ContinuationDoneEvent, &bContinuationRanOnGameThread]() {
                bContinuationRanOnGameThread.store(IsInGameThread());
                ContinuationDoneEvent->Trigger();
            });
        }
    );

    bool bDone = false;
    for (int32 Iter = 0; Iter < 1000; ++Iter) {
        FPlatformProcess::Yield();
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        if (ContinuationDoneEvent->Wait(5)) {
            bDone = true;
            break;
        }
    }

    TestTrue(TEXT("续体在合理时间内执行完毕"), bDone);
    TestTrue(TEXT("后台任务已执行"), bBackgroundRan.load());
    TestTrue(TEXT("主线程续体已在 GameThread 执行"), bContinuationRanOnGameThread.load());

    FPlatformProcess::ReturnSynchEventToPool(ContinuationDoneEvent);
    Owner->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameTaskRunnerInvalidOwnerTest, "Game.Core.Async.Tasks.TaskRunner.InvalidOwner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证宿主对象失效时后台任务与主线程续体自动短路静默丢弃
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 */
bool FGameTaskRunnerInvalidOwnerTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* Owner = NewObject<UGameInstance>();
    Owner->MarkAsGarbage();

    std::atomic<bool> bBackgroundExecuted{false};
    std::atomic<bool> bGameThreadExecuted{false};

    FGameThreadTaskRunner::Run(Owner, [&bBackgroundExecuted]() {
        bBackgroundExecuted.store(true);
    });

    FGameThreadTaskRunner::RunOnGameThread(Owner, [&bGameThreadExecuted]() {
        bGameThreadExecuted.store(true);
    });

    TestFalse(TEXT("宿主失效时后台任务直接短路不执行"), bBackgroundExecuted.load());
    TestFalse(TEXT("宿主失效时主线程续体直接短路不执行"), bGameThreadExecuted.load());

    FGameAsyncTaskHandle ValidOwnerHandle;
    UGameInstance* ActiveOwner = NewObject<UGameInstance>();
    std::atomic<int32> TaskStepCount{0};
    ValidOwnerHandle = FGameThreadTaskRunner::Run(ActiveOwner, [&TaskStepCount]() {
        TaskStepCount.fetch_add(1, std::memory_order_relaxed);
    });
    TestTrue(TEXT("有效宿主派发任务返回有效 FGameAsyncTaskHandle"), ValidOwnerHandle.IsValid());
    TestTrue(TEXT("WaitWithTimeout 等待任务完成返回 true"), ValidOwnerHandle.WaitWithTimeout(FTimespan::FromSeconds(2.0)));
    TestTrue(TEXT("Wait 完成后 IsCompleted 为 true"), ValidOwnerHandle.IsCompleted());
    TestEqual(TEXT("任务执行计数为 1"), TaskStepCount.load(std::memory_order_relaxed), 1);

    FGameAsyncTaskHandle StopHandle = FGameThreadTaskRunner::Run(ActiveOwner, []() {});
    TestTrue(TEXT("Stop 协同取消并等待完成返回 true"), StopHandle.Stop(FTimespan::FromSeconds(2.0)));
    ActiveOwner->MarkAsGarbage();

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameTaskRunnerFlushTest, "Game.Core.Async.Tasks.TaskRunner.FlushInFlightTasks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证主线程安全排空在飞任务并消除自旋死锁
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 */
bool FGameTaskRunnerFlushTest::RunTest([[maybe_unused]] const FString& Parameters) {
    std::atomic<int32> CompletedCount{0};
    for (int32 Index = 0; Index < 5; ++Index) {
        FGameThreadTaskRunner::Run(
            FCancellationToken::None(),
            [&CompletedCount]() {
                FPlatformProcess::Sleep(0.01f);
                CompletedCount.fetch_add(1, std::memory_order_relaxed);
            }
        );
    }

    const bool bFlushed = FGameThreadTaskRunner::FlushInFlightTasks(5.0);
    TestTrue(TEXT("FlushInFlightTasks 超时前排空成功"), bFlushed);
    TestEqual(TEXT("全部 5 个任务均已完成"), CompletedCount.load(std::memory_order_relaxed), 5);
    TestEqual(TEXT("排空后在飞任务数为 0"), FGameThreadTaskRunner::GetInFlightTaskCount(), 0);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
