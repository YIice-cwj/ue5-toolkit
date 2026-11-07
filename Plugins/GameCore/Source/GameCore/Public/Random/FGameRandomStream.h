/**
 * @file FGameRandomStream.h
 * @brief 确定性隔离伪随机流（SplitMix64 种子混合 + PCG32 状态推进，支持存档持久化）
 */

#pragma once

#include "CoreMinimal.h"

#include "FGameRandomStream.generated.h"

/**
 * @brief 64 位状态隔离伪随机数流（跨平台确定性一致，杜绝全局随机流污染）
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FGameRandomStream {
    GENERATED_BODY()

public:
    static constexpr uint64 DEFAULT_SEED = 0x853C49E6748FEA9BULL;
    static constexpr uint64 PCG_MULTIPLIER = 6364136223846793005ULL;
    static constexpr uint64 PCG_INCREMENT = 1442695040888963407ULL;

public:
    /**
     * @brief 默认构造（使用固定确定性默认种子完成混合初始化）
     */
    FGameRandomStream() noexcept;

    /**
     * @brief 使用指定 64 位种子构造并混合初始化流状态
     * @param InSeed 初始种子
     */
    explicit FGameRandomStream(uint64 InSeed) noexcept;

    /**
     * @brief 使用新种子重新混合并初始化内部状态
     * @param InSeed 64 位初始种子
     */
    void Initialize(uint64 InSeed) noexcept;

    /**
     * @brief 推进状态并生成下一个 32 位均匀分布无符号随机整数
     * @return 32 位无符号随机整数
     */
    [[nodiscard]] uint32 NextUInt32() noexcept;

    /**
     * @brief 生成 [0, Bound) 半开区间内无取模偏差的 32 位无符号随机整数
     * @param Bound 上界（为 0 时直接返回 0）
     * @return 无偏随机整数
     */
    [[nodiscard]] uint32 NextBoundedUInt32(uint32 Bound) noexcept;

    /**
     * @brief 生成闭区间 [Min, Max] 内的无偏随机有符号整数（Min > Max 时自动交换）
     * @param Min 区间下界
     * @param Max 区间上界
     * @return 闭区间随机整数
     */
    [[nodiscard]] int32 RandRange(int32 Min, int32 Max) noexcept;

    /**
     * @brief 生成 [0.0f, 1.0f) 半开区间内的单精度均匀随机浮点数
     * @return [0.0f, 1.0f) 随机浮点数
     */
    [[nodiscard]] float FRand() noexcept;

    /**
     * @brief 生成 [Min, Max] 区间内的均匀随机浮点数（Min > Max 时自动交换）
     * @param Min 区间下界
     * @param Max 区间上界
     * @return 随机浮点数
     */
    [[nodiscard]] float FRandRange(float Min, float Max) noexcept;

    /**
     * @brief 按给定概率执行一次伯努利判定
     * @param Probability 成功概率（<=0 恒返回 false，>=1 恒返回 true）
     * @return 命中返回 true
     */
    [[nodiscard]] bool RollChance(float Probability) noexcept;

    /**
     * @brief 在三维单位球面上执行均匀立体角随机方向采样
     * @return 单位长度随机三维方向向量
     */
    [[nodiscard]] FVector RandomUnitVector() noexcept;

    /**
     * @brief 在指定轴向与半张角的圆锥立体角内执行均匀随机方向采样
     * @param ConeDir 圆锥中心轴向量（零向量时返回零向量）
     * @param ConeHalfAngleRad 圆锥半张角弧度（钳制到 [0, PI]）
     * @return 圆锥内单位长度随机方向向量
     */
    [[nodiscard]] FVector RandomVectorInConeRad(const FVector& ConeDir, float ConeHalfAngleRad) noexcept;

    /**
     * @brief 获取当前内部 64 位状态快照（供确定性回放或存档导出）
     * @return 64 位内部状态值
     */
    [[nodiscard]] uint64 GetState() const noexcept {
        return CurrentState;
    }

    /**
     * @brief 直接恢复内部 64 位状态快照（供存档读入或断点复现，不做二次种子混合）
     * @param InState 待恢复的精确状态值
     */
    void SetState(uint64 InState) noexcept {
        CurrentState = InState;
    }

    /**
     * @brief 二进制归档序列化流状态
     * @param Ar 归档器引用
     */
    void SerializeState(FArchive& Ar);

private:
    void AdvanceState() noexcept;

private:
    static uint64 MixSeedSplitMix64(uint64 Seed) noexcept;

private:
    UPROPERTY(SaveGame)
    uint64 CurrentState = DEFAULT_SEED;
};
