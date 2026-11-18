/**
 * @file FScopedDebugStopwatch.h
 * @brief 现代化 RAII 作用域耗时诊断秒表（支持超时告警阈值与编译期零开销裁剪）
 */

#pragma once

#include "CoreMinimal.h"
#include "Config/GameCompileSwitches.h"

/**
 * @brief RAII 作用域耗时诊断秒表（离开作用域时若超出阈值则自动向通道与控制台派发高亮告警）
 */
class GAMEDEBUG_API FScopedDebugStopwatch {
public:
    /**
     * @brief 构造函数：记录当前作用域开始时间戳
     * @param InScopeName 诊断作用域名称
     * @param InThresholdMs 超时告警毫秒阈值（默认 0.0f 表示总是打印）
     * @param InChannel 派发通道名称（默认 "Profiler"）
     */
    FScopedDebugStopwatch(
        const FName& InScopeName,
        float InThresholdMs = 0.0f,
        const FName& InChannel = FName(TEXT("Profiler"))
    );

    FScopedDebugStopwatch(const FScopedDebugStopwatch&) = delete;
    FScopedDebugStopwatch& operator=(const FScopedDebugStopwatch&) = delete;
    FScopedDebugStopwatch(FScopedDebugStopwatch&&) = delete;
    FScopedDebugStopwatch& operator=(FScopedDebugStopwatch&&) = delete;

    /**
     * @brief 析构函数：核算作用域执行总耗时，若超出阈值则触发高亮打印
     */
    ~FScopedDebugStopwatch();

    /**
     * @brief 显式获取当前已流逝的毫秒耗时
     * @return 毫秒耗时
     */
    [[nodiscard]] float GetElapsedMs() const;

private:
    FName ScopeName;
    float ThresholdMs = 0.0f;
    FName Channel;
    double StartTimeSeconds = 0.0;
};

#if GAME_ENABLE_DEBUG_DRAW != 0
    #define GAME_DEBUG_SCOPED_TIMER(ScopeName, ThresholdMs) \
        FScopedDebugStopwatch UE_JOIN(ScopedStopwatch_, __LINE__)(FName(TEXT(ScopeName)), ThresholdMs)

    #define GAME_DEBUG_SCOPED_TIMER_CHANNEL(ScopeName, ThresholdMs, Channel) \
        FScopedDebugStopwatch UE_JOIN(ScopedStopwatch_, __LINE__)(FName(TEXT(ScopeName)), ThresholdMs, FName(TEXT(Channel)))
#else
    #define GAME_DEBUG_SCOPED_TIMER(ScopeName, ThresholdMs) ((void)0)
    #define GAME_DEBUG_SCOPED_TIMER_CHANNEL(ScopeName, ThresholdMs, Channel) ((void)0)
#endif
