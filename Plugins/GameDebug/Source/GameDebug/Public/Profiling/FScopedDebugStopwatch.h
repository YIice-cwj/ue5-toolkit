/**
 * @file FScopedDebugStopwatch.h
 * @brief 现代化 RAII 作用域耗时诊断秒表（复用 GameCore::FScopedCpuTimer，受 GAME_ENABLE_CPU_PROFILER 编译期零开销裁剪）
 */

#pragma once

#include "CoreMinimal.h"
#include "Config/GameCompileSwitches.h"
#include "HAL/PreprocessorHelpers.h"

#if GAME_ENABLE_CPU_PROFILER != 0
    #include "Profiling/FScopedCpuTimer.h"
#endif

/**
 * @brief RAII 作用域耗时诊断秒表（离开作用域时若耗时达到或超出阈值，则通过 GameLogging 与调试屏幕通道派发告警；Shipping 下为空类零成员零时钟开销）
 */
class GAMEDEBUG_API FScopedDebugStopwatch final {
public:
    static constexpr double SevereThresholdMultiplier = 2.0;
    static constexpr float AlertDisplayDurationSeconds = 3.0f;

public:
    /**
     * @brief 构造函数：使用 GameCore::FScopedCpuTimer 记录当前作用域起始硬件周期
     * @param InScopeName 诊断作用域名称
     * @param InThresholdMs 超时告警毫秒阈值（<= 0.0 或非有限值均归零，0.0 表示总是输出告警）
     * @param InChannel 屏幕告警通道名称（默认 "Profiler"）
     */
    explicit FScopedDebugStopwatch(
        FName InScopeName,
        double InThresholdMs = 0.0,
        FName InChannel = FName(TEXT("Profiler"))
    );

    /**
     * @brief 析构函数：核算作用域执行总耗时，若达到或超出阈值（>= ThresholdMs）则触发日志与通道告警
     */
    ~FScopedDebugStopwatch();

    FScopedDebugStopwatch(const FScopedDebugStopwatch&) = delete;
    FScopedDebugStopwatch& operator=(const FScopedDebugStopwatch&) = delete;
    FScopedDebugStopwatch(FScopedDebugStopwatch&&) = delete;
    FScopedDebugStopwatch& operator=(FScopedDebugStopwatch&&) = delete;

    /**
     * @brief 显式获取当前已流逝的毫秒耗时（禁用 GAME_ENABLE_CPU_PROFILER 时恒返回 0.0）
     * @return 双精度毫秒耗时
     */
    [[nodiscard]] double GetElapsedMs() const noexcept;

private:
#if GAME_ENABLE_CPU_PROFILER != 0
    FName ScopeName;
    double ThresholdMs = 0.0;
    FName Channel;
    FScopedCpuTimer CpuTimer;
#endif
};

#if GAME_ENABLE_CPU_PROFILER != 0
    #define GAME_DEBUG_SCOPED_TIMER(ScopeName, ThresholdMs) \
        FScopedDebugStopwatch UE_JOIN(ScopedStopwatch_, __COUNTER__)(FName(TEXT(ScopeName)), (ThresholdMs))

    #define GAME_DEBUG_SCOPED_TIMER_CHANNEL(ScopeName, ThresholdMs, Channel) \
        FScopedDebugStopwatch UE_JOIN(ScopedStopwatch_, __COUNTER__)(FName(TEXT(ScopeName)), (ThresholdMs), FName(TEXT(Channel)))
#else
    #define GAME_DEBUG_SCOPED_TIMER(ScopeName, ThresholdMs) ((void)0)
    #define GAME_DEBUG_SCOPED_TIMER_CHANNEL(ScopeName, ThresholdMs, Channel) ((void)0)
#endif
