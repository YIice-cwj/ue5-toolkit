/**
 * @file FGameMath.h
 * @brief 帧率无关临界阻尼平滑与空间方位几何工具库
 */

#pragma once

#include "CoreMinimal.h"

/**
 * @brief 通用高频游戏数学与空间几何纯静态工具类
 */
class GAMECORE_API FGameMath {
public:
    /**
     * @brief 一维标量临界阻尼弹簧状态（纯值语义，支持结构化绑定）
     */
    struct FSpringFloatState {
        float Value = 0.0f;
        float Velocity = 0.0f;

        bool operator==(const FSpringFloatState&) const = default;
    };

    /**
     * @brief 三维向量临界阻尼弹簧状态（纯值语义，支持结构化绑定）
     */
    struct FSpringVectorState {
        FVector Value = FVector::ZeroVector;
        FVector Velocity = FVector::ZeroVector;

        bool operator==(const FSpringVectorState&) const = default;
    };

public:
    static constexpr float MIN_SMOOTH_TIME = 0.0001f;

public:
    FGameMath() = delete;

public:
    /**
     * @brief 计算一维标量无超调临界阻尼弹簧下一帧状态（帧率无关解析近似，任意大步长绝对稳定）
     * @param CurrentState 当前位置与速度值对象
     * @param TargetValue 目标标量值
     * @param SmoothTime 近似收敛平滑时长（秒，内部下限钳制 0.0001s）
     * @param DeltaSeconds 帧流逝时间（秒，<=0 时原样返回当前状态）
     * @return 更新后的位置与速度状态
     */
    [[nodiscard]] static FSpringFloatState CriticalSpringDampFloat(
        FSpringFloatState CurrentState,
        float TargetValue,
        float SmoothTime,
        float DeltaSeconds
    ) noexcept;

    /**
     * @brief 计算三维向量无超调临界阻尼弹簧下一帧状态
     * @param CurrentState 当前三维位置与速度状态
     * @param TargetValue 目标三维向量
     * @param SmoothTime 近似收敛平滑时长（秒）
     * @param DeltaSeconds 帧流逝时间（秒，<=0 时原样返回当前状态）
     * @return 更新后的三维位置与速度状态
     */
    [[nodiscard]] static FSpringVectorState CriticalSpringDampVector(
        const FSpringVectorState& CurrentState,
        const FVector& TargetValue,
        float SmoothTime,
        float DeltaSeconds
    ) noexcept;

    /**
     * @brief 计算角度标量（度）无超调临界阻尼弹簧下一帧状态（自动沿 [-180, 180] 最短圆弧路径收敛）
     * @param CurrentState 当前角度（度）与角速度（度/秒）
     * @param TargetAngleDegrees 目标角度（度）
     * @param SmoothTime 近似收敛平滑时长（秒）
     * @param DeltaSeconds 帧流逝时间（秒，<=0 时原样返回当前状态）
     * @return 更新后的角度与角速度状态（角度标准化至 [-180, 180]）
     */
    [[nodiscard]] static FSpringFloatState CriticalSpringDampAngleDegrees(
        FSpringFloatState CurrentState,
        float TargetAngleDegrees,
        float SmoothTime,
        float DeltaSeconds
    ) noexcept;

    /**
     * @brief 计算水平面（XY）上从基准朝向旋转至目标方向的有符号偏航夹角（度，范围 [-180, 180]）
     * @param ForwardDir 自身前向向量
     * @param ToTargetDir 指向目标的方向或位移向量
     * @return 有符号水平偏航角（度）；任一向量水平投影接近零时返回 0
     */
    [[nodiscard]] static float SignedYawDeltaDegrees(const FVector& ForwardDir, const FVector& ToTargetDir) noexcept;

    /**
     * @brief 判定目标相对位移是否落在给定距离上限与半张角的圆锥视域内
     * @param OffsetToTarget 从视点指向目标点的位移向量（Target - Eye）
     * @param ConeForward 视锥中心轴朝向向量
     * @param MaxDistance 视锥最大有效半径（<=0 时恒返回 false）
     * @param HalfAngleDegrees 视锥半张角（度，<0 时恒返回 false，[0, 180] 内按夹角判定，>=180 时覆盖全向球体）
     * @return 目标位于视锥距离与夹角范围内返回 true
     */
    [[nodiscard]] static bool IsInVisionCone(
        const FVector& OffsetToTarget,
        const FVector& ConeForward,
        float MaxDistance,
        float HalfAngleDegrees
    ) noexcept;

    /**
     * @brief 基于半衰期参数化的一维标量精确帧率无关指数衰减平滑
     * @param CurrentValue 当前标量值
     * @param TargetValue 目标标量值
     * @param HalfLifeSeconds 误差衰减至一半所需的半衰期时长（秒，<=0 时瞬时收敛）
     * @param DeltaSeconds 帧流逝时间（秒，<=0 时原样返回当前值）
     * @return 更新后的平滑标量值
     */
    [[nodiscard]] static float ExpDecayFloat(
        float CurrentValue,
        float TargetValue,
        float HalfLifeSeconds,
        float DeltaSeconds
    ) noexcept;

    /**
     * @brief 基于半衰期参数化的角度标量（度）精确帧率无关指数衰减平滑（最短圆弧路径）
     * @param CurrentDegrees 当前角度（度）
     * @param TargetDegrees 目标角度（度）
     * @param HalfLifeSeconds 误差衰减至一半所需的半衰期时长（秒，<=0 时瞬时收敛）
     * @param DeltaSeconds 帧流逝时间（秒，<=0 时原样返回当前值）
     * @return 平滑后的角度（度，标准化至 [-180, 180]）
     */
    [[nodiscard]] static float ExpDecayAngleDegrees(
        float CurrentDegrees,
        float TargetDegrees,
        float HalfLifeSeconds,
        float DeltaSeconds
    ) noexcept;

    /**
     * @brief 基于半衰期参数化的三维向量精确帧率无关指数衰减平滑
     * @param CurrentValue 当前三维向量
     * @param TargetValue 目标三维向量
     * @param HalfLifeSeconds 误差衰减至一半所需的半衰期时长（秒，<=0 时瞬时收敛）
     * @param DeltaSeconds 帧流逝时间（秒，<=0 时原样返回当前值）
     * @return 更新后的平滑三维向量
     */
    [[nodiscard]] static FVector ExpDecayVector(
        const FVector& CurrentValue,
        const FVector& TargetValue,
        float HalfLifeSeconds,
        float DeltaSeconds
    ) noexcept;

    /**
     * @brief 基于半衰期参数化的四元数精确帧率无关球面指数衰减平滑（自适应双向最短路径）
     * @param CurrentValue 当前旋转四元数
     * @param TargetValue 目标旋转四元数
     * @param HalfLifeSeconds 误差衰减至一半所需的半衰期时长（秒，<=0 时瞬时收敛）
     * @param DeltaSeconds 帧流逝时间（秒，<=0 时原样返回当前值）
     * @return 更新后的归一化旋转四元数
     */
    [[nodiscard]] static FQuat ExpDecayQuat(
        const FQuat& CurrentValue,
        const FQuat& TargetValue,
        float HalfLifeSeconds,
        float DeltaSeconds
    ) noexcept;
};
