/**
 * @file FPseudoRandomDistribution.cpp
 * @brief FPseudoRandomDistribution 伪随机平滑分布器实现
 */

#include "Random/FPseudoRandomDistribution.h"

#include "Random/FGameRandomStream.h"

/**
 * @brief 按目标标称概率构造平滑分布器
 * @param InNominalProbability 标称概率（钳制到 [0.0f, 1.0f]）
 */
FPseudoRandomDistribution::FPseudoRandomDistribution(float InNominalProbability) noexcept {
    SetNominalProbability(InNominalProbability);
}

/**
 * @brief 更新标称概率并重算递增步长常数 C（保留当前已积累的连续失败次数）
 * @param InNominalProbability 新标称概率（钳制到 [0.0f, 1.0f]）
 */
void FPseudoRandomDistribution::SetNominalProbability(float InNominalProbability) noexcept {
    NominalProbability = FMath::Clamp(InNominalProbability, 0.0f, 1.0f);
    PreciseIncrementConstant = CalculatePreciseConstantForProbability(static_cast<double>(NominalProbability));
    IncrementConstant = static_cast<float>(PreciseIncrementConstant);
}

/**
 * @brief 预览下一次掷骰即将生效的瞬时成功概率（采用双精度步长常数避免高连败或低概率截断偏差）
 * @return 下一次判定概率 [0.0f, 1.0f]
 */
float FPseudoRandomDistribution::GetNextRollProbability() const noexcept {
    if (NominalProbability <= 0.0f) {
        return 0.0f;
    }
    if (NominalProbability >= 1.0f) {
        return 1.0f;
    }
    const int32 ClampedStreak = FMath::Clamp(FailureStreak, 0, MAX_FAILURE_STREAK);
    const int64 NextAttempt = static_cast<int64>(ClampedStreak) + 1LL;
    double EffectiveConstant = (PreciseIncrementConstant > 0.0)
        ? PreciseIncrementConstant
        : static_cast<double>(IncrementConstant);
    if (EffectiveConstant <= 0.0 && NominalProbability > 0.0f) {
        EffectiveConstant = CalculatePreciseConstantForProbability(static_cast<double>(NominalProbability));
    }
    const double RawChance = EffectiveConstant * static_cast<double>(NextAttempt);
    return static_cast<float>(FMath::Clamp(RawChance, 0.0, 1.0));
}

/**
 * @brief 使用给定随机流执行一次平滑掷骰判定
 * @param Stream 隔离随机数流
 * @return 命中返回 true（内部重置连续失败计数），未命中返回 false（递增连续失败计数）
 */
bool FPseudoRandomDistribution::Roll(FGameRandomStream& Stream) noexcept {
    if (NominalProbability <= 0.0f) {
        return false;
    }
    if (NominalProbability >= 1.0f) {
        FailureStreak = 0;
        return true;
    }

    const float CurrentChance = GetNextRollProbability();
    if (Stream.RollChance(CurrentChance)) {
        FailureStreak = 0;
        return true;
    }

    if (FailureStreak < MAX_FAILURE_STREAK) {
        ++FailureStreak;
    }
    return false;
}

/**
 * @brief 通过双精度二分逼近求解给定标称概率对应的 PRD 步长常数 C（单精度接口）
 * @param Probability 标称概率 [0.0f, 1.0f]
 * @return 精确匹配的步长常数 C
 */
float FPseudoRandomDistribution::CalculateConstantForProbability(float Probability) noexcept {
    return static_cast<float>(CalculatePreciseConstantForProbability(static_cast<double>(Probability)));
}

/**
 * @brief 通过双精度二分逼近求解给定标称概率对应的全精度 PRD 步长常数 C
 * @param Probability 标称概率 [0.0, 1.0]
 * @return 双精度步长常数 C
 */
double FPseudoRandomDistribution::CalculatePreciseConstantForProbability(double Probability) noexcept {
    if (Probability <= 0.0) {
        return 0.0;
    }
    if (Probability >= 1.0) {
        return 1.0;
    }

    const double TargetP = Probability;
    double Low = 0.0;
    double High = TargetP;
    double Mid = TargetP;

    for (int32 Iteration = 0; Iteration < 40; ++Iteration) {
        Mid = 0.5 * (Low + High);
        const double EffectiveP = EvaluateEffectiveProbability(Mid);
        if (EffectiveP < TargetP) {
            Low = Mid;
        } else {
            High = Mid;
        }
    }

    return Mid;
}

/**
 * @brief 评估给定步长常数 C 下的理论宏观有效触发概率
 * @param ConstantC 步长常数 C
 * @return 理论有效触发概率
 */
double FPseudoRandomDistribution::EvaluateEffectiveProbability(double ConstantC) noexcept {
    if (ConstantC <= 0.0) {
        return 0.0;
    }
    const int32 MaxAttempts = FMath::CeilToInt(1.0 / ConstantC);
    double ExpectedTrials = 0.0;
    double SurvivalProbability = 1.0;

    for (int32 Attempt = 1; Attempt <= MaxAttempts; ++Attempt) {
        const double StepProbability = FMath::Min(1.0, static_cast<double>(Attempt) * ConstantC);
        ExpectedTrials += static_cast<double>(Attempt) * StepProbability * SurvivalProbability;
        SurvivalProbability *= (1.0 - StepProbability);
        if (SurvivalProbability <= 0.0) {
            break;
        }
    }

    return ExpectedTrials > 0.0 ? (1.0 / ExpectedTrials) : 0.0;
}
