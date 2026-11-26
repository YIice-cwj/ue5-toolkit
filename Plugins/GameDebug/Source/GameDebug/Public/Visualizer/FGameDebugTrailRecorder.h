/**
 * @file FGameDebugTrailRecorder.h
 * @brief 现代化 3D 空间运动轨迹记录器（O(1) 定长环形淘汰、真实时间衰减颜色渐变与编译期零开销剥离）
 */

#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * @brief 3D 空间运动轨迹记录器（用于弹道投掷物、突进位移技能、网络预测回滚与怪物寻路历史可视化）
 */
class GAMEDEBUG_API FGameDebugTrailRecorder final {
public:
    struct FTrailSample {
        FVector Location = FVector::ZeroVector;
        double Timestamp = 0.0;
    };

    static constexpr double UsePlatformClockSentinel = -1.0;

    /**
     * @brief 构造运动轨迹记录器（一次性分配定长环形缓冲，稳态写入与淘汰严格 O(1) 零堆分配）
     * @param InMaxSamples 最大保留采样点数量（下限钳制为 2）
     * @param InLifetimeSeconds 单点生命周期最大秒数（下限钳制为 0.1f）
     * @param InMinDistance 触发新采样的最小空间距离阈值（下限钳制为 0.0f，0.0 表示不做距离去重）
     */
    explicit FGameDebugTrailRecorder(
        int32 InMaxSamples = 64,
        float InLifetimeSeconds = 2.0f,
        float InMinDistance = 10.0f
    );

    /**
     * @brief 记录一个新的空间位置采样点（自动拦截 NaN/Inf 坐标与非有限时间戳、对回退时间戳做单调非递减钳制，先清理超期点再执行距离去重与 O(1) 环形写入）
     * @param NewLocation 最新空间坐标
     * @param CurrentTimeSeconds 自定义模拟时间戳（>= 0.0 直接采用含 t=0.0；负数哨兵表示使用平台高精度时钟）
     * @return 产生新采样点返回 true
     */
    bool AddSample(const FVector& NewLocation, double CurrentTimeSeconds = UsePlatformClockSentinel);

    /**
     * @brief 清理超期采样点并绘制当前有效生命周期内的平滑运动轨迹（颜色沿剩余生命周期时间渐变衰减）
     * @param World 目标世界
     * @param HeadColor 轨迹头部颜色（最新采样时刻）
     * @param TailColor 轨迹尾部颜色（濒临过期淘汰时刻）
     * @param Thickness 线条粗细
     * @param Duration 持续秒数（0 为单帧）
     * @param CurrentTimeSeconds 自定义模拟时间戳（>= 0.0 直接采用含 t=0.0；负数哨兵表示使用平台高精度时钟）
     */
    void DrawTrail(
        const UWorld* World,
        const FColor& HeadColor = FColor::Green,
        const FColor& TailColor = FColor::Blue,
        float Thickness = 2.0f,
        float Duration = 0.0f,
        double CurrentTimeSeconds = UsePlatformClockSentinel
    );

    /**
     * @brief 清空已记录的全部轨迹采样点（保留底层定长缓冲，O(1) 复位）
     */
    void Clear() noexcept;

    /**
     * @brief 获取当前有效采样点数量
     * @return 采样点数量
     */
    [[nodiscard]] int32 GetSampleCount() const noexcept;

    /**
     * @brief 获取指定逻辑索引处的采样点（0 为最旧点，GetSampleCount() - 1 为最新点）
     * @param Index 逻辑索引
     * @return 采样点常引用
     */
    [[nodiscard]] const FTrailSample& GetSample(int32 Index) const;

private:
    /**
     * @brief 按给定时间基准 O(1) 推进环形头指针淘汰所有超期采样点
     * @param CurrentTimeSeconds 当前时间基准（秒）
     */
    void PruneExpired(double CurrentTimeSeconds) noexcept;

    /**
     * @brief 将逻辑索引映射为定长环形缓冲中的物理槽位索引
     * @param LogicalIndex 逻辑索引（[0, SampleCount)）
     * @return 物理槽位索引
     */
    [[nodiscard]] int32 ToPhysicalIndex(int32 LogicalIndex) const noexcept;

private:
    TArray<FTrailSample> RingBuffer;
    int32 MaxSamples = 64;
    int32 HeadIndex = 0;
    int32 SampleCount = 0;
    float LifetimeSeconds = 2.0f;
    double MinDistanceSquared = 100.0;
};
