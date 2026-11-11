/**
 * @file FGameMath.cpp
 * @brief FGameMath 帧率无关阻尼平滑与空间几何工具实现
 */

#include "Math/FGameMath.h"

/**
 * @brief 计算一维标量无超调临界阻尼弹簧下一帧状态
 * @param CurrentState 当前位置与速度值对象
 * @param TargetValue 目标标量值
 * @param SmoothTime 近似收敛平滑时长（秒）
 * @param DeltaSeconds 帧流逝时间（秒）
 * @return 更新后的位置与速度状态
 */
FGameMath::FSpringFloatState FGameMath::CriticalSpringDampFloat(
    FSpringFloatState CurrentState,
    float TargetValue,
    float SmoothTime,
    float DeltaSeconds
) noexcept {
    if (DeltaSeconds <= 0.0f) {
        return CurrentState;
    }

    const float ClampedSmoothTime = FMath::Max(MIN_SMOOTH_TIME, SmoothTime);
    const float Omega = 2.0f / ClampedSmoothTime;
    const float X = Omega * DeltaSeconds;
    const float X2 = X * X;
    const float ExpFactor = 1.0f / (1.0f + X + 0.48f * X2 + 0.235f * X2 * X);

    const float DeltaValue = CurrentState.Value - TargetValue;
    const float Temp = (CurrentState.Velocity + Omega * DeltaValue) * DeltaSeconds;
    const float NextVelocity = (CurrentState.Velocity - Omega * Temp) * ExpFactor;
    const float NextValue = TargetValue + (DeltaValue + Temp) * ExpFactor;

    return FSpringFloatState{NextValue, NextVelocity};
}

/**
 * @brief 计算三维向量无超调临界阻尼弹簧下一帧状态
 * @param CurrentState 当前三维位置与速度状态
 * @param TargetValue 目标三维向量
 * @param SmoothTime 近似收敛平滑时长（秒）
 * @param DeltaSeconds 帧流逝时间（秒）
 * @return 更新后的三维位置与速度状态
 */
FGameMath::FSpringVectorState FGameMath::CriticalSpringDampVector(
    const FSpringVectorState& CurrentState,
    const FVector& TargetValue,
    float SmoothTime,
    float DeltaSeconds
) noexcept {
    if (DeltaSeconds <= 0.0f) {
        return CurrentState;
    }

    const double ClampedSmoothTime = static_cast<double>(FMath::Max(MIN_SMOOTH_TIME, SmoothTime));
    const double Dt = static_cast<double>(DeltaSeconds);
    const double Omega = 2.0 / ClampedSmoothTime;
    const double X = Omega * Dt;
    const double X2 = X * X;
    const double ExpFactor = 1.0 / (1.0 + X + 0.48 * X2 + 0.235 * X2 * X);

    const FVector DeltaVec = CurrentState.Value - TargetValue;
    const FVector TempVec = (CurrentState.Velocity + DeltaVec * Omega) * Dt;
    const FVector NextVelocity = (CurrentState.Velocity - TempVec * Omega) * ExpFactor;
    const FVector NextValue = TargetValue + (DeltaVec + TempVec) * ExpFactor;

    return FSpringVectorState{NextValue, NextVelocity};
}

/**
 * @brief 计算水平面（XY）上从基准朝向旋转至目标方向的有符号偏航夹角（度，范围 [-180, 180]）
 * @param ForwardDir 自身前向向量
 * @param ToTargetDir 指向目标的方向或位移向量
 * @return 有符号水平偏航角（度）
 */
float FGameMath::SignedYawDeltaDegrees(const FVector& ForwardDir, const FVector& ToTargetDir) noexcept {
    const FVector2D FlatForward(ForwardDir.X, ForwardDir.Y);
    const FVector2D FlatTarget(ToTargetDir.X, ToTargetDir.Y);

    const double ForwardLenSq = FlatForward.SizeSquared();
    const double TargetLenSq = FlatTarget.SizeSquared();
    if (ForwardLenSq <= UE_DOUBLE_SMALL_NUMBER || TargetLenSq <= UE_DOUBLE_SMALL_NUMBER) {
        return 0.0f;
    }

    const double CrossZ = FlatForward.X * FlatTarget.Y - FlatForward.Y * FlatTarget.X;
    const double DotXY = FlatForward.X * FlatTarget.X + FlatForward.Y * FlatTarget.Y;
    const double Radians = FMath::Atan2(CrossZ, DotXY);
    return static_cast<float>(FMath::RadiansToDegrees(Radians));
}

/**
 * @brief 判定目标相对位移是否落在给定距离上限与半张角的圆锥视域内（先执行距离平方早退与全向短路，避免无效开方）
 * @param OffsetToTarget 从视点指向目标点的位移向量
 * @param ConeForward 视锥中心轴朝向向量
 * @param MaxDistance 视锥最大有效半径
 * @param HalfAngleDegrees 视锥半张角（度）
 * @return 目标位于视锥距离与夹角范围内返回 true
 */
bool FGameMath::IsInVisionCone(
    const FVector& OffsetToTarget,
    const FVector& ConeForward,
    float MaxDistance,
    float HalfAngleDegrees
) noexcept {
    if (MaxDistance <= 0.0f || HalfAngleDegrees < 0.0f) {
        return false;
    }

    const double DistSq = OffsetToTarget.SizeSquared();
    const double MaxDistSq = static_cast<double>(MaxDistance) * static_cast<double>(MaxDistance);
    if (DistSq > MaxDistSq) {
        return false;
    }

    const double AxisLenSq = ConeForward.SizeSquared();
    if (AxisLenSq <= UE_DOUBLE_SMALL_NUMBER) {
        return false;
    }
    if (DistSq <= UE_DOUBLE_KINDA_SMALL_NUMBER) {
        return true;
    }

    const float ClampedHalfAngle = FMath::Clamp(HalfAngleDegrees, 0.0f, 180.0f);
    if (ClampedHalfAngle >= 180.0f) {
        return true;
    }

    const double InvDistAndAxis = FMath::InvSqrt(DistSq * AxisLenSq);
    const double CosAngle = FVector::DotProduct(ConeForward, OffsetToTarget) * InvDistAndAxis;
    const double CosThreshold = FMath::Cos(FMath::DegreesToRadians(static_cast<double>(ClampedHalfAngle)));
    return CosAngle >= (CosThreshold - 1e-6);
}

namespace {

constexpr double EXP_DECAY_LN2 = 0.693147180559945309417;

inline float ComputeExpDecayFactor(float HalfLifeSeconds, float DeltaSeconds) noexcept {
    if (DeltaSeconds <= 0.0f) {
        return 0.0f;
    }
    if (HalfLifeSeconds <= 0.00001f) {
        return 1.0f;
    }
    const double Lambda = EXP_DECAY_LN2 / static_cast<double>(HalfLifeSeconds);
    const double Decay = FMath::Exp(-Lambda * static_cast<double>(DeltaSeconds));
    return static_cast<float>(FMath::Clamp(1.0 - Decay, 0.0, 1.0));
}

} // namespace

/**
 * @brief 基于半衰期参数化的一维标量精确帧率无关指数衰减平滑
 * @param CurrentValue 当前标量值
 * @param TargetValue 目标标量值
 * @param HalfLifeSeconds 误差衰减至一半所需的半衰期时长（秒）
 * @param DeltaSeconds 帧流逝时间（秒）
 * @return 更新后的平滑标量值
 */
float FGameMath::ExpDecayFloat(
    float CurrentValue,
    float TargetValue,
    float HalfLifeSeconds,
    float DeltaSeconds
) noexcept {
    if (!FMath::IsFinite(CurrentValue) || !FMath::IsFinite(TargetValue)) {
        return TargetValue;
    }
    const float Factor = ComputeExpDecayFactor(HalfLifeSeconds, DeltaSeconds);
    if (Factor <= 0.0f) {
        return CurrentValue;
    }
    if (Factor >= 1.0f) {
        return TargetValue;
    }
    return CurrentValue + (TargetValue - CurrentValue) * Factor;
}

/**
 * @brief 基于半衰期参数化的三维向量精确帧率无关指数衰减平滑
 * @param CurrentValue 当前三维向量
 * @param TargetValue 目标三维向量
 * @param HalfLifeSeconds 误差衰减至一半所需的半衰期时长（秒）
 * @param DeltaSeconds 帧流逝时间（秒）
 * @return 更新后的平滑三维向量
 */
FVector FGameMath::ExpDecayVector(
    const FVector& CurrentValue,
    const FVector& TargetValue,
    float HalfLifeSeconds,
    float DeltaSeconds
) noexcept {
    if (CurrentValue.ContainsNaN() || TargetValue.ContainsNaN()) {
        return TargetValue;
    }
    const float Factor = ComputeExpDecayFactor(HalfLifeSeconds, DeltaSeconds);
    if (Factor <= 0.0f) {
        return CurrentValue;
    }
    if (Factor >= 1.0f) {
        return TargetValue;
    }
    return CurrentValue + (TargetValue - CurrentValue) * Factor;
}

/**
 * @brief 基于半衰期参数化的四元数精确帧率无关球面指数衰减平滑
 * @param CurrentValue 当前旋转四元数
 * @param TargetValue 目标旋转四元数
 * @param HalfLifeSeconds 误差衰减至一半所需的半衰期时长（秒）
 * @param DeltaSeconds 帧流逝时间（秒）
 * @return 更新后的归一化旋转四元数
 */
FQuat FGameMath::ExpDecayQuat(
    const FQuat& CurrentValue,
    const FQuat& TargetValue,
    float HalfLifeSeconds,
    float DeltaSeconds
) noexcept {
    if (CurrentValue.ContainsNaN() || TargetValue.ContainsNaN()) {
        return TargetValue;
    }
    const float Factor = ComputeExpDecayFactor(HalfLifeSeconds, DeltaSeconds);
    if (Factor <= 0.0f) {
        return CurrentValue;
    }
    if (Factor >= 1.0f) {
        return TargetValue;
    }

    FQuat TargetAdjusted = TargetValue;
    const double Dot = CurrentValue | TargetValue;
    if (Dot < 0.0) {
        TargetAdjusted = -TargetValue;
    }
    return FQuat::Slerp(CurrentValue, TargetAdjusted, Factor).GetNormalized();
}
