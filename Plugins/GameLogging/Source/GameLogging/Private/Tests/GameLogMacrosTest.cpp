// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameLogMacrosTest.cpp
 * @brief 日志宏并发安全性、条件短路、确定性限频、抑制前置短路、变量遮蔽防护与槽位动态分发测试
 */

#include "Logging/GameLogCategories.h"

#if WITH_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Containers/Set.h"
#include "GameLogTestCaptureDevice.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformTLS.h"
#include "Misc/AutomationTest.h"
#include "Types/TCancelableScopeGuard.h"

#include <atomic>

namespace {

DEFINE_LOG_CATEGORY_STATIC(LogUnitTestCatA, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogUnitTestCatB, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogUnitTestSuppressCat, Log, All);

using Game::Log::Test::FGameLogCaptureDevice;

/**
 * @brief 执行并发工作组单任务迭代，辅助压平测试嵌套深度
 * @param TaskIndex 当前任务编号
 * @param IterationsPerTask 单任务执行步数
 * @param CompletedWorkers 完成计数原子变量
 * @param ThreadIdsLock 线程 ID 集合保护锁
 * @param ThreadIds 已参与执行的线程 ID 集合
 */
void RunConcurrentMacroWorkerStep(
    int32 TaskIndex,
    int32 IterationsPerTask,
    std::atomic<int32>& CompletedWorkers,
    FCriticalSection& ThreadIdsLock,
    TSet<uint32>& ThreadIds
) {
    {
        FScopeLock ScopeLock(&ThreadIdsLock);
        ThreadIds.Add(FPlatformTLS::GetCurrentThreadId());
    }
    for (int32 Step = 0; Step < IterationsPerTask; ++Step) {
        const int32 Index = TaskIndex * IterationsPerTask + Step;
        GAME_LOG(Log, TEXT("Parallel log emission index: %d"), Index);
        GAME_CLOG(Index % 2 == 0, Log, TEXT("Parallel clog emission index: %d"), Index);
        GAME_LOG_ONCE(Log, TEXT("Parallel once emission (should only appear once)"));
        GAME_CLOG_ONCE(Index >= 0, Log, TEXT("Parallel clog_once emission (should only appear once)"));
        GAME_LOG_THROTTLE(10.0, Log, TEXT("Parallel throttle emission (interval 10s)"));
        CompletedWorkers.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLogMacrosConcurrencyTest,
    "Game.Logging.Macros.Concurrency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证全部日志宏多线程并发执行时的精确输出次数、去重语义与限频安全性
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过时返回 true
 */
bool FGameLogMacrosConcurrencyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    constexpr int32 WorkerCount = 64;
    constexpr int32 TaskCount = 16;
    constexpr int32 IterationsPerTask = WorkerCount / TaskCount;
    static bool bHasCompletedTestRun = false;

    for (int32 IterationIndex = 0; IterationIndex < 2; ++IterationIndex) {
        const bool bIsFirstEverIteration = !bHasCompletedTestRun && IterationIndex == 0;
        const int32 ExpectedOnceCount = bIsFirstEverIteration ? 1 : 0;
        std::atomic<int32> CompletedWorkers{0};
        FCriticalSection ThreadIdsLock;
        TSet<uint32> ThreadIds;

        FGameLogCaptureDevice LogCapture(TEXT("Parallel log emission index:"));
        FGameLogCaptureDevice ClogCapture(TEXT("Parallel clog emission index:"));
        FGameLogCaptureDevice OnceCapture(TEXT("Parallel once emission"));
        FGameLogCaptureDevice ClogOnceCapture(TEXT("Parallel clog_once emission"));
        FGameLogCaptureDevice ThrottleCapture(TEXT("Parallel throttle emission"));

        TArray<TFuture<void>> Futures;
        Futures.Reserve(TaskCount);
        for (int32 TaskIndex = 0; TaskIndex < TaskCount; ++TaskIndex) {
            Futures.Add(Async(
                EAsyncExecution::ThreadPool,
                [TaskIndex, IterationsPerTask, &CompletedWorkers, &ThreadIdsLock, &ThreadIds]() {
                    RunConcurrentMacroWorkerStep(
                        TaskIndex,
                        IterationsPerTask,
                        CompletedWorkers,
                        ThreadIdsLock,
                        ThreadIds
                    );
                }
            ));
        }

        for (TFuture<void>& Future : Futures) {
            Future.Get();
        }

        TestEqual(TEXT("全部并发工作线程均应安全完成日志宏调用"), CompletedWorkers.load(std::memory_order_relaxed), WorkerCount);
        TestTrue(TEXT("日志宏应实际跨多个线程执行"), ThreadIds.Num() > 1);
        TestEqual(TEXT("GAME_LOG 应精确输出全部 64 次"), LogCapture.GetTotalCount(), WorkerCount);
        TestEqual(TEXT("GAME_CLOG 应精确输出满足偶数条件的 32 次"), ClogCapture.GetTotalCount(), WorkerCount / 2);
        TestEqual(
            FString::Printf(TEXT("GAME_LOG_ONCE 第 %d 轮输出次数"), IterationIndex + 1),
            OnceCapture.GetTotalCount(),
            ExpectedOnceCount
        );
        TestEqual(
            FString::Printf(TEXT("GAME_CLOG_ONCE 第 %d 轮输出次数"), IterationIndex + 1),
            ClogOnceCapture.GetTotalCount(),
            ExpectedOnceCount
        );
        if (bIsFirstEverIteration) {
            TestEqual(TEXT("GAME_LOG_THROTTLE 首轮应恰好输出一次"), ThrottleCapture.GetTotalCount(), 1);
        } else {
            TestTrue(
                FString::Printf(TEXT("GAME_LOG_THROTTLE 第 %d 轮至多输出一次"), IterationIndex + 1),
                ThrottleCapture.GetTotalCount() <= 1
            );
        }
        TestEqual(
            TEXT("并发调用后当前模块分类仍应保持有效"),
            &Game::Log::FGameLogRegistry::GetCategory(UE_MODULE_NAME),
            &Game::Log::FGameLogRegistry::GetFallbackCategory()
        );
    }

    bHasCompletedTestRun = true;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLogMacrosSemanticsTest,
    "Game.Logging.Macros.SemanticsAndSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 GAME_CLOG_ONCE 条件翻转与短路、确定性限频跨窗口重触发、抑制态不消耗节流窗口以及宏变量防遮蔽
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过时返回 true
 */
bool FGameLogMacrosSemanticsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const auto RestoreGuard = MakeCancelableScopeGuard([]() {
        Game::Log::FGameLogRegistry::RegisterCategory(UE_MODULE_NAME, &LogGame);
        LogUnitTestSuppressCat.SetVerbosity(ELogVerbosity::Log);
    });

    static bool bHasRunClogOnceSection = false;
    int32 ConditionEvalCount = 0;
    const auto EvalCondition = [&ConditionEvalCount](bool bShouldTrigger) -> bool {
        ++ConditionEvalCount;
        return bShouldTrigger;
    };

    FGameLogCaptureDevice ClogOnceFlipCapture(TEXT("ClogOnceFlipVerified"));
    const auto EmitClogOnceFlip = [&EvalCondition](bool bTrigger) {
        GAME_CLOG_ONCE(EvalCondition(bTrigger), Log, TEXT("ClogOnceFlipVerified"));
    };

    if (!bHasRunClogOnceSection) {
        EmitClogOnceFlip(false);
        EmitClogOnceFlip(false);
        TestEqual(TEXT("条件为假时不得输出且已评估两次条件"), ClogOnceFlipCapture.GetTotalCount(), 0);
        TestEqual(TEXT("条件为假期间应评估条件表达式 2 次"), ConditionEvalCount, 2);

        EmitClogOnceFlip(true);
        TestEqual(TEXT("条件首次翻转为真时应恰好输出 1 次"), ClogOnceFlipCapture.GetTotalCount(), 1);
        TestEqual(TEXT("首次翻转为真应累计评估条件 3 次"), ConditionEvalCount, 3);

        EmitClogOnceFlip(true);
        EmitClogOnceFlip(false);
        TestEqual(TEXT("已触发后再次调用不得重复输出"), ClogOnceFlipCapture.GetTotalCount(), 1);
        TestEqual(TEXT("TTAS 短路生效后不得继续求值条件表达式"), ConditionEvalCount, 3);
        bHasRunClogOnceSection = true;
    }

    std::atomic<double> DeterministicLastTime{-1.0e9};
    TestTrue(TEXT("负哨兵初值保证 t=0.0 首触即打"), Game::Log::Private::TryConsumeThrottleAt(DeterministicLastTime, 0.0, 1.0));
    TestFalse(TEXT("t=0.5 未达间隔应被限频拦截"), Game::Log::Private::TryConsumeThrottleAt(DeterministicLastTime, 0.5, 1.0));
    TestFalse(TEXT("t=0.999 未达间隔应被限频拦截"), Game::Log::Private::TryConsumeThrottleAt(DeterministicLastTime, 0.999, 1.0));
    TestTrue(TEXT("t=1.0 恰好到达间隔应放行第二次输出"), Game::Log::Private::TryConsumeThrottleAt(DeterministicLastTime, 1.0, 1.0));
    TestFalse(TEXT("t=1.5 再次进入冷却应被拦截"), Game::Log::Private::TryConsumeThrottleAt(DeterministicLastTime, 1.5, 1.0));
    TestTrue(TEXT("t=2.2 跨过间隔应再次放行"), Game::Log::Private::TryConsumeThrottleAt(DeterministicLastTime, 2.2, 1.0));
    TestTrue(TEXT("零间隔应允许同时间戳立即再次放行"), Game::Log::Private::TryConsumeThrottleAt(DeterministicLastTime, 2.2, 0.0));

    std::atomic<double> ContendedLastTime{-1.0e9};
    std::atomic<int32> CasWinners{0};
    ParallelFor(32, [&ContendedLastTime, &CasWinners](int32) {
        if (Game::Log::Private::TryConsumeThrottleAt(ContendedLastTime, 100.0, 1.0)) {
            CasWinners.fetch_add(1, std::memory_order_relaxed);
        }
    });
    TestEqual(TEXT("32 线程同时间戳竞争限频窗口应恰好仅 1 个胜出者"), CasWinners.load(), 1);

    FGameLogCaptureDevice ZeroIntervalCapture(TEXT("ZeroIntervalThrottleVerified"));
    for (int32 Index = 0; Index < 3; ++Index) {
        GAME_LOG_THROTTLE(0.0, Log, TEXT("ZeroIntervalThrottleVerified #%d"), Index);
    }
    TestEqual(TEXT("0 秒间隔限频宏应放行全部 3 次输出"), ZeroIntervalCapture.GetTotalCount(), 3);

    static bool bHasRunSuppressThrottleSection = false;
    if (!bHasRunSuppressThrottleSection) {
        Game::Log::FGameLogRegistry::RegisterCategory(UE_MODULE_NAME, &LogUnitTestSuppressCat);
        LogUnitTestSuppressCat.SetVerbosity(ELogVerbosity::Log);

        FGameLogCaptureDevice SuppressThrottleCapture(TEXT("SuppressedThrottleWindowVerified"));
        const auto EmitVerboseThrottled = []() {
            GAME_LOG_THROTTLE(60.0, Verbose, TEXT("SuppressedThrottleWindowVerified"));
        };

        EmitVerboseThrottled();
        TestEqual(TEXT("Verbose 被抑制期间不得输出"), SuppressThrottleCapture.GetTotalCount(), 0);

        LogUnitTestSuppressCat.SetVerbosity(ELogVerbosity::Verbose);
        EmitVerboseThrottled();
        TestEqual(TEXT("抑制期未消耗节流窗口，开启 Verbose 后首次调用应立即输出"), SuppressThrottleCapture.GetTotalCount(), 1);
        EmitVerboseThrottled();
        TestEqual(TEXT("开启后同窗口第二次调用应被 60 秒限频拦截"), SuppressThrottleCapture.GetTotalCount(), 1);

        bHasRunSuppressThrottleSection = true;
    }

    Game::Log::FGameLogRegistry::RegisterCategory(UE_MODULE_NAME, &LogGame);
    FGameLogCaptureDevice ShadowCapture(TEXT("ShadowCheck:"));
    const double CurrentSeconds = 42.5;
    const double LastSeconds = 17.25;
    const int32 CurrentEpoch = 7;
    const int32 CachedEpoch = 9;
    const TCHAR* LogCatPtr = TEXT("CallerVar");
    GAME_LOG(Log, TEXT("ShadowCheck: %.2f %.2f %d %d %s"), CurrentSeconds, LastSeconds, CurrentEpoch, CachedEpoch, LogCatPtr);
    TestEqual(TEXT("GAME_LOG 不得遮蔽调用方同名局部变量"), ShadowCapture.GetLastMessage(), FString(TEXT("ShadowCheck: 42.50 17.25 7 9 CallerVar")));

    FGameLogCaptureDevice ThrottleShadowCapture(TEXT("ThrottleShadow:"));
    GAME_LOG_THROTTLE(0.0, Log, TEXT("ThrottleShadow: %.2f %.2f"), CurrentSeconds, LastSeconds);
    TestEqual(TEXT("GAME_LOG_THROTTLE 不得遮蔽调用方 CurrentSeconds/LastSeconds"), ThrottleShadowCapture.GetLastMessage(), FString(TEXT("ThrottleShadow: 42.50 17.25")));

    LogUnitTestSuppressCat.SetVerbosity(ELogVerbosity::Log);
    TestFalse(TEXT("Fatal 在 Log 级别下不得被抑制"), Game::Log::Private::IsCategorySuppressed(LogUnitTestSuppressCat, ELogVerbosity::Fatal));
    TestFalse(TEXT("Error 在 Log 级别下不得被抑制"), Game::Log::Private::IsCategorySuppressed(LogUnitTestSuppressCat, ELogVerbosity::Error));
    TestFalse(TEXT("Log 在 Log 级别下不得被抑制"), Game::Log::Private::IsCategorySuppressed(LogUnitTestSuppressCat, ELogVerbosity::Log));
    TestTrue(TEXT("Verbose 在 Log 级别下应被抑制"), Game::Log::Private::IsCategorySuppressed(LogUnitTestSuppressCat, ELogVerbosity::Verbose));
    TestTrue(TEXT("VeryVerbose 在 Log 级别下应被抑制"), Game::Log::Private::IsCategorySuppressed(LogUnitTestSuppressCat, ELogVerbosity::VeryVerbose));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLogDynamicDispatchSlotTest,
    "Game.Logging.Macros.SlotDispatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证同一宏调用点通过常驻槽位在注销、注册、覆写后即时且无撕裂地输出至最新分类
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过时返回 true
 */
bool FGameLogDynamicDispatchSlotTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const char* ModuleName = UE_MODULE_NAME;
    const auto RestoreGuard = MakeCancelableScopeGuard([ModuleName]() {
        Game::Log::FGameLogRegistry::RegisterCategory(ModuleName, &LogGame);
    });

    FLogCategoryBase& FallbackCat = Game::Log::FGameLogRegistry::GetFallbackCategory();
    FGameLogCaptureDevice Capture(TEXT("Slot dispatch verified emission"));
    const auto EmitLog = []() {
        GAME_LOG(Log, TEXT("Slot dispatch verified emission"));
    };

    Game::Log::FGameLogRegistry::UnregisterCategory(ModuleName);
    EmitLog();
    TestEqual(TEXT("同一调用点未注册基线应输出到兜底分类"), Capture.GetLastCategory(), FallbackCat.GetCategoryName());

    Game::Log::FGameLogRegistry::RegisterCategory(ModuleName, &LogUnitTestCatA);
    EmitLog();
    TestEqual(TEXT("同一调用点注册后应即时输出到分类 A"), Capture.GetLastCategory(), LogUnitTestCatA.GetCategoryName());

    Game::Log::FGameLogRegistry::UnregisterCategory(ModuleName);
    EmitLog();
    TestEqual(TEXT("同一调用点注销后应即时回退到兜底分类"), Capture.GetLastCategory(), FallbackCat.GetCategoryName());

    Game::Log::FGameLogRegistry::RegisterCategory(ModuleName, &LogUnitTestCatB);
    EmitLog();
    TestEqual(TEXT("同一调用点覆写后应即时输出到分类 B"), Capture.GetLastCategory(), LogUnitTestCatB.GetCategoryName());
    TestEqual(TEXT("同一调用点四次动态分发均应精确输出"), Capture.GetTotalCount(), 4);

    return true;
}

#endif // WITH_AUTOMATION_TESTS
