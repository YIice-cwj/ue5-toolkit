/**
 * @file FPseudoRandomDistribution.cpp
 * @brief FPseudoRandomDistribution 伪随机平滑分布器实现
 */

#include "Random/FPseudoRandomDistribution.h"

#include "Random/FGameRandomStream.h"

FPseudoRandomDistribution::FPseudoRandomDistribution(float InNominalProbability) noexcept {
    SetNominalProbability(InNominalProbability);
}

void FPseudoRandomDistribution::SetNominalProbability(float InNominalProbability) noexcept {
    NominalProbability = FMath::Clamp(InNominalProbability, 0.0f, 1.0f);
    IncrementConstant = CalculateConstantForProbability(NominalProbability);
}

float FPseudoRandomDistribution::GetNextRollProbability() const noexcept {
    if (NominalProbability <= 0.0f) {
        return 0.0f;
    }
    if (NominalProbability >= 1.0f) {
        return 1.0f;
    }
    const int32 NextAttempt = FailureStreak + 1;
    return FMath::Clamp(IncrementConstant * static_cast<float>(NextAttempt), 0.0f, 1.0f);
}

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

    ++FailureStreak;
    return false;
}

float FPseudoRandomDistribution::CalculateConstantForProbability(float Probability) noexcept {
    if (Probability <= 0.0f) {
        return 0.0f;
    }
    if (Probability >= 1.0f) {
        return 1.0f;
    }

    const double TargetP = static_cast<double>(Probability);
    double Low = 0.0;
    double High = TargetP;
    double Mid = TargetP;

    for (int32 Iteration = 0; Iteration < 32; ++Iteration) {
        Mid = 0.5 * (Low + High);
        const double EffectiveP = EvaluateEffectiveProbability(Mid);
        if (EffectiveP < TargetP) {
            Low = Mid;
        } else {
            High = Mid;
        }
    }

    return static_cast<float>(Mid);
}

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
