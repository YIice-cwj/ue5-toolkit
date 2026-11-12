/**
 * @file TWeightedPicker.h
 * @brief 基于 Vose's Alias Method 的 O(1) 权重随机抽取器模板
 */

#pragma once

#include "CoreMinimal.h"

#include "Random/FGameRandomStream.h"

/**
 * @brief 恒定时间 O(1) 权重轮盘抽取容器模板（O(N) 惰性构建别名表）
 * @tparam TValue 候选元素值类型
 */
template <typename TValue>
class TWeightedPicker {
public:
    using ValueType = TValue;
    using SizeType = int32;

private:
    struct FWeightedEntry {
        ValueType Value;
        double Weight = 0.0;
    };

public:
    TWeightedPicker() = default;

    /**
     * @brief 预分配候选条目存储容量
     * @param Capacity 预期条目数
     */
    void Reserve(SizeType Capacity);

    /**
     * @brief 添加一条带正权重的候选元素（非正数或非有限权重将被拒绝）
     * @param Item 候选元素值
     * @param Weight 正权重值（必须 > 0 且有限）
     * @return 成功添加返回 true，权重非法返回 false
     */
    bool AddEntry(ValueType Item, double Weight);

    /**
     * @brief 显式预构建底层 Vose 别名表（消除首次抽取时的惰性构建开销）
     */
    void Build() const;

    /**
     * @brief 以 O(1) 复杂度按权重抽取一个元素指针（空表返回 nullptr）
     * @param Stream 驱动抽取的隔离随机流
     * @return 命中元素的常量指针，空表返回 nullptr
     */
    [[nodiscard]] const ValueType* TryPick(FGameRandomStream& Stream) const;

    /**
     * @brief 以 O(1) 复杂度按权重抽取一个元素引用（空表时触发断言，不确定非空请用 TryPick）
     * @param Stream 驱动抽取的隔离随机流
     * @return 命中元素的常量引用
     */
    [[nodiscard]] const ValueType& Pick(FGameRandomStream& Stream) const;

    /**
     * @brief 以 O(1) 复杂度按权重抽取一个元素值拷贝（安全值语义，避免持有悬垂引用）
     * @param Stream 驱动抽取的隔离随机流
     * @return 命中元素的值拷贝
     */
    [[nodiscard]] ValueType PickValue(FGameRandomStream& Stream) const {
        return Pick(Stream);
    }

    /**
     * @brief 清空所有候选条目与别名表缓存
     */
    void Clear();

    /**
     * @brief 获取有效候选条目数量
     * @return 条目数
     */
    [[nodiscard]] SizeType Num() const noexcept {
        return Entries.Num();
    }

    /**
     * @brief 检查抽取器是否为空
     * @return 无候选条目返回 true
     */
    [[nodiscard]] bool IsEmpty() const noexcept {
        return Entries.IsEmpty();
    }

    /**
     * @brief 获取当前所有候选条目的总权重
     * @return 权重总和
     */
    [[nodiscard]] double GetTotalWeight() const noexcept {
        return TotalWeight;
    }

    /**
     * @brief 获取指定索引候选条目的权重
     * @param Index 条目索引
     * @return 权重数值（非法索引返回 0.0）
     */
    [[nodiscard]] double GetWeight(SizeType Index) const noexcept {
        return Entries.IsValidIndex(Index) ? Entries[Index].Weight : 0.0;
    }

private:
    TArray<FWeightedEntry> Entries;
    double TotalWeight = 0.0;
    mutable TArray<double> ProbabilityTable;
    mutable TArray<int32> AliasTable;
    mutable bool bAliasBuilt = false;
};

template <typename TValue>
void TWeightedPicker<TValue>::Reserve(SizeType Capacity) {
    if (Capacity > 0) {
        Entries.Reserve(Capacity);
    }
}

template <typename TValue>
bool TWeightedPicker<TValue>::AddEntry(ValueType Item, double Weight) {
    if (!FMath::IsFinite(Weight) || Weight <= 0.0 || !FMath::IsFinite(TotalWeight + Weight)) {
        return false;
    }
    Entries.Add(FWeightedEntry{MoveTemp(Item), Weight});
    TotalWeight += Weight;
    bAliasBuilt = false;
    return true;
}

template <typename TValue>
void TWeightedPicker<TValue>::Build() const {
    if (bAliasBuilt) {
        return;
    }

    const int32 Count = Entries.Num();
    ProbabilityTable.SetNumUninitialized(Count);
    AliasTable.SetNumUninitialized(Count);

    if (Count == 0) {
        bAliasBuilt = true;
        return;
    }

    TArray<double> ScaledProbabilities;
    ScaledProbabilities.SetNumUninitialized(Count);

    TArray<int32> SmallIndices;
    TArray<int32> LargeIndices;
    SmallIndices.Reserve(Count);
    LargeIndices.Reserve(Count);

    const double Scale = static_cast<double>(Count) / TotalWeight;
    for (int32 Index = 0; Index < Count; ++Index) {
        const double Scaled = Entries[Index].Weight * Scale;
        ScaledProbabilities[Index] = Scaled;
        if (Scaled < 1.0) {
            SmallIndices.Add(Index);
        } else {
            LargeIndices.Add(Index);
        }
    }

    while (!SmallIndices.IsEmpty() && !LargeIndices.IsEmpty()) {
        const int32 SmallIdx = SmallIndices.Pop(EAllowShrinking::No);
        const int32 LargeIdx = LargeIndices.Pop(EAllowShrinking::No);

        ProbabilityTable[SmallIdx] = ScaledProbabilities[SmallIdx];
        AliasTable[SmallIdx] = LargeIdx;

        ScaledProbabilities[LargeIdx] = (ScaledProbabilities[LargeIdx] + ScaledProbabilities[SmallIdx]) - 1.0;
        if (ScaledProbabilities[LargeIdx] < 1.0) {
            SmallIndices.Add(LargeIdx);
        } else {
            LargeIndices.Add(LargeIdx);
        }
    }

    while (!LargeIndices.IsEmpty()) {
        const int32 LargeIdx = LargeIndices.Pop(EAllowShrinking::No);
        ProbabilityTable[LargeIdx] = 1.0;
        AliasTable[LargeIdx] = LargeIdx;
    }

    while (!SmallIndices.IsEmpty()) {
        const int32 SmallIdx = SmallIndices.Pop(EAllowShrinking::No);
        ProbabilityTable[SmallIdx] = 1.0;
        AliasTable[SmallIdx] = SmallIdx;
    }

    bAliasBuilt = true;
}

template <typename TValue>
const typename TWeightedPicker<TValue>::ValueType* TWeightedPicker<TValue>::TryPick(FGameRandomStream& Stream) const {
    if (Entries.IsEmpty()) {
        return nullptr;
    }
    if (!bAliasBuilt) {
        Build();
    }

    const int32 Column = Stream.RandRange(0, Entries.Num() - 1);
    const double CoinToss = static_cast<double>(Stream.FRand());
    const int32 ChosenIndex = (CoinToss < ProbabilityTable[Column]) ? Column : AliasTable[Column];
    return &Entries[ChosenIndex].Value;
}

template <typename TValue>
const typename TWeightedPicker<TValue>::ValueType& TWeightedPicker<TValue>::Pick(FGameRandomStream& Stream) const {
    const ValueType* Picked = TryPick(Stream);
    checkf(Picked != nullptr, TEXT("Attempted Pick() on an empty TWeightedPicker"));
    return *Picked;
}

template <typename TValue>
void TWeightedPicker<TValue>::Clear() {
    Entries.Reset();
    TotalWeight = 0.0;
    ProbabilityTable.Reset();
    AliasTable.Reset();
    bAliasBuilt = false;
}
