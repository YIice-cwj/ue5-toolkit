// Copyright Epic Games, Inc. All Rights Reserved.

#include "Logging/GameLogMacros.h"

#if WITH_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Containers/Set.h"
#include "GameLogTestCaptureDevice.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformTLS.h"
#include "Misc/AutomationTest.h"

#include <atomic>

namespace {

DEFINE_LOG_CATEGORY_STATIC(LogUnitTestCatA, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogUnitTestCatB, Log, All);

using Game::Log::Test::FGameLogCaptureDevice;

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameLogMacrosConcurrencyTest, "Game.Logging.Macros.Concurrency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证并发日志宏安全性，以及 ONCE 和 THROTTLE 在同进程重复执行时的输出语义
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过时返回 true，否则返回 false
 */
bool FGameLogMacrosConcurrencyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    constexpr int32 WorkerCount = 64;
    static bool bHasCompletedTestRun = false;

    for (int32 IterationIndex = 0; IterationIndex < 2; ++IterationIndex) {
        const bool bIsFirstEverIteration = !bHasCompletedTestRun && IterationIndex == 0;
        const int32 ExpectedOnceCount = bIsFirstEverIteration ? 1 : 0;
        std::atomic<int32> CompletedWorkers{0};
        FCriticalSection ThreadIdsLock;
        TSet<uint32> ThreadIds;
        FGameLogCaptureDevice OnceCapture(TEXT("Parallel once emission"));
        FGameLogCaptureDevice ClogOnceCapture(TEXT("Parallel clog_once emission"));
        FGameLogCaptureDevice ThrottleCapture(TEXT("Parallel throttle emission"));

        constexpr int32 TaskCount = 16;
        constexpr int32 IterationsPerTask = WorkerCount / TaskCount;
        TArray<TFuture<void>> Futures;
        Futures.Reserve(TaskCount);

        for (int32 TaskIndex = 0; TaskIndex < TaskCount; ++TaskIndex) {
            Futures.Add(Async(EAsyncExecution::ThreadPool, [&CompletedWorkers, &ThreadIdsLock, &ThreadIds, TaskIndex]() {
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
            }));
        }

        for (TFuture<void>& Future : Futures) {
            Future.Get();
        }

        TestEqual(TEXT("全部并发工作线程均应安全完成日志宏调用"),
            CompletedWorkers.load(std::memory_order_relaxed),
            WorkerCount);
        TestTrue(TEXT("日志宏应实际跨多个线程执行"), ThreadIds.Num() > 1);
        TestEqual(FString::Printf(TEXT("GAME_LOG_ONCE 第 %d 轮输出次数"), IterationIndex + 1),
            OnceCapture.GetTotalCount(), ExpectedOnceCount);
        TestEqual(FString::Printf(TEXT("GAME_CLOG_ONCE 第 %d 轮输出次数"), IterationIndex + 1),
            ClogOnceCapture.GetTotalCount(), ExpectedOnceCount);
        if (bIsFirstEverIteration) {
            TestEqual(TEXT("GAME_LOG_THROTTLE 首轮应恰好输出一次"), ThrottleCapture.GetTotalCount(), 1);
        } else {
            TestTrue(FString::Printf(TEXT("GAME_LOG_THROTTLE 第 %d 轮至多输出一次"), IterationIndex + 1),
                ThrottleCapture.GetTotalCount() <= 1);
        }
        TestEqual(TEXT("并发调用后当前模块分类仍应保持有效"),
            &Game::Log::FGameLogRegistry::GetCategory(UE_MODULE_NAME),
            &Game::Log::FGameLogRegistry::GetFallbackCategory());
    }

    bHasCompletedTestRun = true;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameLogDynamicDispatchEpochTest, "Game.Logging.Macros.EpochDispatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证同一宏调用点在注册、覆写和注销后输出到最新分类，并恢复模块默认登记
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过时返回 true，否则返回 false
 */
bool FGameLogDynamicDispatchEpochTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const char* ModuleName = UE_MODULE_NAME;
    FLogCategoryBase& FallbackCat = Game::Log::FGameLogRegistry::GetFallbackCategory();
    FGameLogCaptureDevice Capture(TEXT("Epoch dispatch verified emission"));
    const auto EmitLog = [] {
        GAME_LOG(Log, TEXT("Epoch dispatch verified emission"));
    };

    Game::Log::FGameLogRegistry::UnregisterCategory(ModuleName);
    EmitLog();
    TestEqual(TEXT("同一调用点基线应输出到兜底分类"),
        Capture.GetLastCategory(), FallbackCat.GetCategoryName());
    const int32 EpochInitial = Game::Log::FGameLogRegistry::GetEpoch();

    Game::Log::FGameLogRegistry::RegisterCategory(ModuleName, &LogUnitTestCatA);
    EmitLog();
    TestEqual(TEXT("同一调用点注册后应输出到分类 A"),
        Capture.GetLastCategory(), LogUnitTestCatA.GetCategoryName());
    TestTrue(TEXT("覆写分类后纪元号应递增"),
        Game::Log::FGameLogRegistry::GetEpoch() > EpochInitial);

    Game::Log::FGameLogRegistry::UnregisterCategory(ModuleName);
    EmitLog();
    TestEqual(TEXT("同一调用点注销后应输出到兜底分类"),
        Capture.GetLastCategory(), FallbackCat.GetCategoryName());

    Game::Log::FGameLogRegistry::RegisterCategory(ModuleName, &LogUnitTestCatB);
    EmitLog();
    TestEqual(TEXT("同一调用点覆写后应输出到分类 B"),
        Capture.GetLastCategory(), LogUnitTestCatB.GetCategoryName());
    TestEqual(TEXT("同一调用点四次动态分发均应实际输出"), Capture.GetTotalCount(), 4);

    Game::Log::FGameLogRegistry::RegisterCategory(ModuleName, &LogGame);
    TestEqual(TEXT("测试结束后应恢复 GameLogging 模块默认分类注册"),
        &Game::Log::FGameLogRegistry::GetCategory(ModuleName),
        static_cast<FLogCategoryBase*>(&LogGame));

    return true;
}

#endif // WITH_AUTOMATION_TESTS
