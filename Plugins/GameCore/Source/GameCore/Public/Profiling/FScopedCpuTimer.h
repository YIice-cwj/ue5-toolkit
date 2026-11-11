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
     * @brief 重置计时器起始点
     */
    void Reset() noexcept {
        StartCycles = FPlatformTime::Cycles64();
    }

private:
    uint64 StartCycles;
    FCompleteCallback Callback;
};

/**
 * @brief 固定窗口连续滑动平均采样统计器
 * @tparam WindowSize 滑动窗口采样项数上限（默认 64 项）
 */
template <int32 WindowSize = 64>
class FMovingAverageSampler {
    static_assert(WindowSize > 0, "WindowSize must be greater than zero.");

public:
    FMovingAverageSampler() noexcept {
        Reset();
    }

    /**
     * @brief 压入单次采样测量值
     * @param SampleValue 测量值
     */
    void AddSample(double SampleValue) noexcept {
        if (SampleCount < WindowSize) {
            Samples[SampleCount] = SampleValue;
            RunningSum += SampleValue;
            ++SampleCount;
        } else {
            RunningSum -= Samples[WriteIndex];
            Samples[WriteIndex] = SampleValue;
            RunningSum += SampleValue;
            WriteIndex = (WriteIndex + 1) % WindowSize;
        }

        MinSample = (SampleCount == 1) ? SampleValue : FMath::Min(MinSample, SampleValue);
        MaxSample = (SampleCount == 1) ? SampleValue : FMath::Max(MaxSample, SampleValue);
    }

    /**
     * @brief 获取当前滑动窗口平均值
     * @return 算术平均值；无采样时返回 0.0
     */
    [[nodiscard]] double GetAverage() const noexcept {
        return (SampleCount > 0) ? (RunningSum / SampleCount) : 0.0;
    }

    /**
     * @brief 获取历史最小采样值
     * @return 最小值；无采样时返回 0.0
     */
    [[nodiscard]] double GetMin() const noexcept {
        return (SampleCount > 0) ? MinSample : 0.0;
    }

    /**
     * @brief 获取历史最大采样值
     * @return 最大值；无采样时返回 0.0
     */
    [[nodiscard]] double GetMax() const noexcept {
        return (SampleCount > 0) ? MaxSample : 0.0;
    }

    /**
     * @brief 获取当前有效采样数
     * @return 采样项数
     */
    [[nodiscard]] int32 GetSampleCount() const noexcept {
        return SampleCount;
    }

    /**
     * @brief 重置统计器状态
     */
    void Reset() noexcept {
        SampleCount = 0;
        WriteIndex = 0;
        RunningSum = 0.0;
        MinSample = 0.0;
        MaxSample = 0.0;
    }

private:
    double Samples[WindowSize]{0.0};
    int32 SampleCount = 0;
    int32 WriteIndex = 0;
    double RunningSum = 0.0;
    double MinSample = 0.0;
    double MaxSample = 0.0;
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
