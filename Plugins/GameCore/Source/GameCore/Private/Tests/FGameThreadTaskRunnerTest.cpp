/**
 * @file FGameThreadTaskRunnerTest.cpp
 * @brief 异步任务调度与主线程续体工具自动化测试，Shipping 构建自动剔除
 */

#include "Tasks/FGameThreadTaskRunner.h"

#if WITH_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameTaskRunnerBasicContinuationTest, "Game.Core.Tasks.TaskRunner.BasicContinuation",
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

    FGameThreadTaskRunner::RunWithContinuation<int32>(
        Owner,
        []() {
            return 42;
        },
        [ContinuationDoneEvent, &bContinuationExecutedOnGameThread, &ReceivedResult](int32 Result) {
            bContinuationExecutedOnGameThread.store(IsInGameThread());
            ReceivedResult.store(Result);
            ContinuationDoneEvent->Trigger();
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameTaskRunnerVoidContinuationTest, "Game.Core.Tasks.TaskRunner.VoidContinuation",
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

    FGameThreadTaskRunner::RunWithContinuation(
        Owner,
        [&bBackgroundRan]() {
            bBackgroundRan.store(true);
        },
        [ContinuationDoneEvent, &bContinuationRanOnGameThread]() {
            bContinuationRanOnGameThread.store(IsInGameThread());
            ContinuationDoneEvent->Trigger();
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameTaskRunnerInvalidOwnerTest, "Game.Core.Tasks.TaskRunner.InvalidOwner",
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

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
