/**
 * @file FScopedCpuTimer.h
 * @brief 零开销 RAII CPU 微秒计时器与滑动平均采样器（供时间切片调度器与底层算法微观性能评测）
 */

#pragma once

#include "CoreMinimal.h"
#include "Config/GameCompileSwitches.h"
#include "HAL/PlatformTime.h"

/**
 * @brief 纯 RAII 高精度 CPU 作用域计时器
 */
class FScopedCpuTimer {
public:
    using FCompleteCallback = TFunction<void(double ElapsedMicroseconds)>;

    /**
     * @brief 默认构造函数（仅启动计时，不注册收尾回调）
     */
    FScopedCpuTimer() noexcept
        : StartCycles(FPlatformTime::Cycles64())
        , Callback(nullptr) {
    }

    /**
     * @brief 注册完成回调的构造函数
     * @param InCallback 作用域析构时触发的耗时传出闭包（参数为微秒）
     */
    explicit FScopedCpuTimer(FCompleteCallback InCallback) noexcept
        : StartCycles(FPlatformTime::Cycles64())
        , Callback(MoveTemp(InCallback)) {
    }

    ~FScopedCpuTimer() {
        if (Callback) {
            Callback(GetElapsedMicroseconds());
        }
    }

    FScopedCpuTimer(const FScopedCpuTimer&) = delete;
    FScopedCpuTimer& operator=(const FScopedCpuTimer&) = delete;

    /**
     * @brief 获取当前流逝秒数
     * @return 流逝秒数
     */
    [[nodiscard]] double GetElapsedSeconds() const noexcept {
        const uint64 CurrentCycles = FPlatformTime::Cycles64();
        return (CurrentCycles - StartCycles) * FPlatformTime::GetSecondsPerCycle64();
    }

    /**
     * @brief 获取当前流逝毫秒数
     * @return 流逝毫秒数
     */
    [[nodiscard]] double GetElapsedMilliseconds() const noexcept {
        return GetElapsedSeconds() * 1000.0;
    }

    /**
     * @brief 获取当前流逝微秒数
     * @return 流逝微秒数
     */
    [[nodiscard]] double GetElapsedMicroseconds() const noexcept {
        return GetElapsedSeconds() * 1000000.0;
    }

    /**
     * @brief 获取计时起始周期计数值
     * @return 起始 Cycles64 计数
     */
    [[nodiscard]] uint64 GetStartCycles() const noexcept {
        return StartCycles;
    }

    /**
     * @brief 重置计时器起始点
     */
    void Reset() noexcept {
        StartCycles = FPlatformTime::Cycles64();
    }

private:
    uint64 StartCycles;
    FCompleteCallback Callback;
};

#if GAME_ENABLE_CPU_PROFILER
/**
 * @brief 作用域 CPU 耗时微秒采样宏（受 GAME_ENABLE_CPU_PROFILER 集中编译开关控制）
 */
#define GAME_SCOPED_CPU_TIMER(TimerName, OnCompleteLambda) \
    FScopedCpuTimer TimerName(OnCompleteLambda)
#else
/**
 * @brief 作用域 CPU 耗时微秒采样宏（禁用时完全擦除，零开销）
 */
#define GAME_SCOPED_CPU_TIMER(TimerName, OnCompleteLambda)
#endif
