/**
 * @file FScopedDebugStopwatchTest.cpp
 * @brief FScopedDebugStopwatch RAII 作用域耗时秒表全分支真测试（静音快路径、超阈值橙色告警、重度超时红色告警与宏展开）
 */

#include "Profiling/FScopedDebugStopwatch.h"

#include "Config/GameCompileSwitches.h"
#include "Print/FGameDebugPrint.h"
#include "Tests/GameDebugTestUtils.h"

#if WITH_AUTOMATION_TESTS

#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FScopedDebugStopwatchTest,
    "Game.Debug.Profiling.ScopedStopwatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FScopedDebugStopwatch 未超阈值静默、超阈值橙色告警、重度超时红色告警、负/NaN 阈值归零及 __COUNTER__ 宏无冲突
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FScopedDebugStopwatchTest::RunTest([[maybe_unused]] const FString& Parameters) {
    if constexpr (!Game::Config::bEnableCpuProfiler) {
        FScopedDebugStopwatch DisabledWatch(FName(TEXT("Disabled")), 0.0);
        TestEqual(TEXT("禁用 CPU Profiler 时耗时恒为 0.0"), DisabledWatch.GetElapsedMs(), 0.0);
        return true;
    }

    Game::Debug::Test::FScopedPrintCapture PrintCapture;
    const FName SilentChannel(TEXT("StopwatchSilent"));
    const FName WarnChannel(TEXT("StopwatchWarn"));
    const FName SevereChannel(TEXT("StopwatchSevere"));

    {
        Game::Debug::Test::FScopedDebugLogCapture SilentLog(TEXT("SilentScopeNoAlert"));
        {
            FScopedDebugStopwatch Stopwatch(FName(TEXT("SilentScopeNoAlert")), 100000.0, SilentChannel);
            TestTrue(TEXT("流逝耗时大于等于 0.0 ms"), Stopwatch.GetElapsedMs() >= 0.0);
        }
        TestEqual(TEXT("未达到阈值时零日志告警"), SilentLog.GetTotalCount(), 0);
        TestFalse(TEXT("未达到阈值时零引擎屏幕通道写入"), Game::Debug::Test::HasKeyedScreenMessage(FGameDebugPrint::GetChannelKey(SilentChannel)));
    }

    {
        Game::Debug::Test::FScopedDebugLogCapture AlwaysLog(TEXT("AlwaysWarnScope"));
        {
            FScopedDebugStopwatch Stopwatch(FName(TEXT("AlwaysWarnScope")), 0.0, WarnChannel);
        }
        TestEqual(TEXT("阈值 0.0 必然触发 1 条 LogGameDebug Warning"), AlwaysLog.GetTotalCount(), 1);
        TestEqual(TEXT("秒表日志统一归属 LogGameDebug"), AlwaysLog.GetLastCategory(), FName(TEXT("LogGameDebug")));
        TestEqual(TEXT("秒表日志级别为 Warning"), AlwaysLog.GetLastVerbosity(), ELogVerbosity::Warning);
        TestTrue(TEXT("日志正文包含作用域名称"), AlwaysLog.GetLastMessage().Contains(TEXT("AlwaysWarnScope")));

        if constexpr (Game::Config::bEnableDebugDraw) {
            const uint64 WarnKey = FGameDebugPrint::GetChannelKey(WarnChannel);
            TestTrue(TEXT("达到阈值时同步写入指定引擎屏幕通道"), Game::Debug::Test::HasKeyedScreenMessage(WarnKey));
            Game::Debug::Test::FCapturedPrintRecord WarnMsg;
            TestTrue(TEXT("捕获到橙色告警屏幕派发"), PrintCapture.FindLastByKey(WarnKey, WarnMsg));
            TestEqual(TEXT("普通超阈值为橙色告警"), WarnMsg.Color, FColor::Orange);
            TestTrue(TEXT("屏幕正文包含作用域名称"), WarnMsg.Message.Contains(TEXT("AlwaysWarnScope")));
        }
    }

    {
        Game::Debug::Test::FScopedDebugLogCapture SevereLog(TEXT("SevereOvertimeScope"));
        {
            FScopedDebugStopwatch Stopwatch(FName(TEXT("SevereOvertimeScope")), 0.0001, SevereChannel);
            volatile uint64 SpinSink = 0;
            const double TargetSec = FPlatformTime::Seconds() + 0.0005;
            while (FPlatformTime::Seconds() < TargetSec) {
                SpinSink = SpinSink + 1;
            }
            TestTrue(TEXT("耗时显著超过 2 倍阈值"), Stopwatch.GetElapsedMs() >= 0.0002);
        }
        TestEqual(TEXT("重度超时触发 1 条日志告警"), SevereLog.GetTotalCount(), 1);
        if constexpr (Game::Config::bEnableDebugDraw) {
            const uint64 SevereKey = FGameDebugPrint::GetChannelKey(SevereChannel);
            TestTrue(TEXT("重度超时写入引擎屏幕通道"), Game::Debug::Test::HasKeyedScreenMessage(SevereKey));
            Game::Debug::Test::FCapturedPrintRecord SevereMsg;
            TestTrue(TEXT("捕获到重度超时红色屏幕派发"), PrintCapture.FindLastByKey(SevereKey, SevereMsg));
            TestEqual(TEXT("耗时达到 2 倍阈值触发红色重度告警"), SevereMsg.Color, FColor::Red);
        }
    }

    {
        Game::Debug::Test::FScopedDebugLogCapture SanitizeLog(TEXT("SanitizeScope"));
        {
            FScopedDebugStopwatch NegWatch(FName(TEXT("SanitizeScopeNeg")), -10.0, WarnChannel);
            FScopedDebugStopwatch NaNWatch(FName(TEXT("SanitizeScopeNaN")), std::numeric_limits<double>::quiet_NaN(), WarnChannel);
        }
        TestEqual(TEXT("负数与 NaN 阈值归零并均触发告警"), SanitizeLog.GetTotalCount(), 2);
    }

    {
        Game::Debug::Test::FScopedDebugLogCapture MacroLog(TEXT("MacroCounterScope"));
        {
            GAME_DEBUG_SCOPED_TIMER("MacroCounterScopeA", 0.0); GAME_DEBUG_SCOPED_TIMER_CHANNEL("MacroCounterScopeB", 0.0, "MacroChannel");
        }
        TestEqual(TEXT("同行双宏借助 __COUNTER__ 无冲突并均触发告警"), MacroLog.GetTotalCount(), 2);
    }

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
