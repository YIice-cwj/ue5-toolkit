/**
 * @file FPseudoRandomDistribution.h
 * @brief 伪随机平滑分布器（PRD 算法：维持宏观期望概率严格恒定，抑制极端连黑与连爆）
 */

#pragma once

#include "CoreMinimal.h"

#include "FPseudoRandomDistribution.generated.h"

struct FGameRandomStream;

/**
 * @brief 伪随机平滑分布状态机（每次未命中线性递增下一击概率，命中立即清零回滚）
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FPseudoRandomDistribution {
    GENERATED_BODY()

public:
    static constexpr int32 MAX_FAILURE_STREAK = 1000000;

public:
    FPseudoRandomDistribution() = default;

    /**
     * @brief 按目标标称概率构造平滑分布器
     * @param InNominalProbability 标称概率（钳制到 [0.0f, 1.0f]）
     */
    explicit FPseudoRandomDistribution(float InNominalProbability) noexcept;

    /**
     * @brief 更新标称概率并重算递增步长常数 C（保留当前已积累的连续失败次数）
     * @param InNominalProbability 新标称概率（钳制到 [0.0f, 1.0f]）
     */
    void SetNominalProbability(float InNominalProbability) noexcept;

    /**
     * @brief 获取当前标称概率
     * @return 标称概率 [0.0f, 1.0f]
     */
    [[nodiscard]] float GetNominalProbability() const noexcept {
        return NominalProbability;
    }

    /**
     * @brief 获取与标称概率对应的基础概率递增常数 C
     * @return 步长常数 C
     */
    [[nodiscard]] float GetIncrementConstant() const noexcept {
        return IncrementConstant;
    }

    /**
     * @brief 获取当前已连续未命中的累计次数
     * @return 连续失败次数
     */
    [[nodiscard]] int32 GetFailureStreak() const noexcept {
        return FailureStreak;
    }

    /**
     * @brief 预览下一次掷骰即将生效的瞬时成功概率
     * @return 下一次判定概率 [0.0f, 1.0f]
     */
    [[nodiscard]] float GetNextRollProbability() const noexcept;

    /**
     * @brief 使用给定随机流执行一次平滑掷骰判定
     * @param Stream 隔离随机数流
     * @return 命中返回 true（内部重置连续失败计数），未命中返回 false（递增连续失败计数）
     */
    [[nodiscard]] bool Roll(FGameRandomStream& Stream) noexcept;

    /**
     * @brief 清零连续未命中计数（保持标称概率与常数 C 不变）
     */
    void Reset() noexcept {
        FailureStreak = 0;
    }

public:
    /**
     * @brief 通过双精度二分逼近求解给定标称概率对应的 PRD 步长常数 C（单精度接口）
     * @param Probability 标称概率 [0.0f, 1.0f]
     * @return 精确匹配的步长常数 C
     */
    [[nodiscard]] static float CalculateConstantForProbability(float Probability) noexcept;

    /**
     * @brief 通过双精度二分逼近求解给定标称概率对应的全精度 PRD 步长常数 C（消除极小概率下的单精度截断误差）
     * @param Probability 标称概率 [0.0, 1.0]
     * @return 双精度步长常数 C
     */
    [[nodiscard]] static double CalculatePreciseConstantForProbability(double Probability) noexcept;

private:
    /**
     * @brief 精确计算给定 PRD 递增步长常数 C 对应的长期宏观期望概率
     * @param ConstantC 递增步长常数 C
     * @return 对应的宏观期望成功概率
     */
    static double EvaluateEffectiveProbability(double ConstantC) noexcept;

private:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GameCore|Random", meta = (AllowPrivateAccess = "true"))
    float NominalProbability = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameCore|Random", meta = (AllowPrivateAccess = "true"))
    float IncrementConstant = 0.0f;

    UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category = "GameCore|Random", meta = (AllowPrivateAccess = "true"))
    int32 FailureStreak = 0;

    double PreciseIncrementConstant = 0.0;
};
