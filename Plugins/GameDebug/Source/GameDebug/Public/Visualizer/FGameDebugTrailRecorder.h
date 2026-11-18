/**
 * @file FGameDebugTrailRecorder.h
 * @brief 现代化 3D 空间运动轨迹记录器（支持平滑历史连线、时间衰减颜色渐变与编译期零开销剥离）
 */

#pragma once

#include "CoreMinimal.h"
#include "Config/GameCompileSwitches.h"

class UWorld;

/**
 * @brief 3D 空间运动轨迹记录器（用于弹道投掷物、突进位移技能、网络预测回滚与怪物寻路历史可视化）
 */
class GAMEDEBUG_API FGameDebugTrailRecorder {
public:
    struct FTrailSample {
        FVector Location = FVector::ZeroVector;
        double Timestamp = 0.0;
    };

public:
    /**
     * @brief 构造运动轨迹记录器
     * @param InMaxSamples 最大保留采样点数量
     * @param InLifetimeSeconds 单点生命周期最大秒数（超出自动淘汰）
     * @param InMinDistance 触发新采样的最小空间距离阈值（防原地静止重复堆叠点）
     */
    explicit FGameDebugTrailRecorder(
        int32 InMaxSamples = 64,
        float InLifetimeSeconds = 2.0f,
        float InMinDistance = 10.0f
    );

    /**
     * @brief 记录一个新的空间位置采样点
     * @param NewLocation 最新空间坐标
     * @param CurrentTimeSeconds 自定义模拟时间戳（默认 0.0 表示使用平台高精度时钟）
     * @return 产生新采样点返回 true
     */
    bool AddSample(const FVector& NewLocation, double CurrentTimeSeconds = 0.0);

    /**
     * @brief 绘制当前有效生命周期内的历史平滑运动轨迹（沿时间渐变衰减）
     * @param World 目标世界
     * @param HeadColor 轨迹头部颜色（最新位置）
     * @param TailColor 轨迹尾部颜色（渐隐淘汰边缘）
     * @param Thickness 线条粗细
     * @param Duration 持续秒数（0 为单帧）
     * @param CurrentTimeSeconds 自定义模拟时间戳（默认 0.0 表示使用平台高精度时钟）
     */
    void DrawTrail(
        const UWorld* World,
        const FColor& HeadColor = FColor::Green,
        const FColor& TailColor = FColor::Blue,
        float Thickness = 2.0f,
        float Duration = 0.0f,
        double CurrentTimeSeconds = 0.0
    );

    /**
     * @brief 清空已记录的全部轨迹采样点
     */
    void Clear();

    /**
     * @brief 获取当前有效采样点数量
     * @return 点数量
     */
    [[nodiscard]] int32 GetSampleCount() const noexcept;

    /**
     * @brief 获取指定索引处的采样点
     * @param Index 索引
     * @return 采样点结构
     */
    [[nodiscard]] const FTrailSample& GetSample(int32 Index) const;

private:
    TArray<FTrailSample> Samples;
    int32 MaxSamples = 64;
    float LifetimeSeconds = 2.0f;
    float MinDistanceSquared = 100.0f;
};
