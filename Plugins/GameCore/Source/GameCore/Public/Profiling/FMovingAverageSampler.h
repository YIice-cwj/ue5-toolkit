// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

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
     * @brief 查询采样器是否为空
     * @return 无采样数据返回 true
     */
    [[nodiscard]] bool IsEmpty() const noexcept {
        return SampleCount == 0;
    }

    /**
     * @brief 获取滑动窗口容量上限
     * @return 窗口大小
     */
    [[nodiscard]] static constexpr int32 GetCapacity() noexcept {
        return WindowSize;
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
