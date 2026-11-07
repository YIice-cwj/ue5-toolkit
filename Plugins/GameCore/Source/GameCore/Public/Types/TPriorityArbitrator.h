/**
 * @file TPriorityArbitrator.h
 * @brief 多源优先级属性仲裁器模板（支持最高优先级覆盖、加权累加与链式乘算裁决）
 */

#pragma once

#include "CoreMinimal.h"

/**
 * @brief 属性仲裁求值模式
 */
enum class EPriorityArbitrationMode : uint8 {
    OverrideHighestPriority,
    Additive,
    Multiplicative
};

/**
 * @brief 泛型多通道优先级属性仲裁器模板
 * @tparam T 属性值类型
 */
template <typename T>
class TPriorityArbitrator {
public:
    using ValueType = T;

private:
    struct FChannelRecord {
        ValueType Value{};
        int32 Priority = 0;
        uint64 Sequence = 0;
    };

public:
    TPriorityArbitrator() = default;

    /**
     * @brief 注册或更新指定通道的属性值及其请求优先级
     * @param ChannelName 来源通道标识
     * @param InValue 请求属性值
     * @param InPriority 优先级数值（数值越大优先级越高，默认 0）
     */
    void SetChannel(const FName& ChannelName, const ValueType& InValue, int32 InPriority = 0) {
        FChannelRecord& Record = Channels.FindOrAdd(ChannelName);
        Record.Value = InValue;
        Record.Priority = InPriority;
        Record.Sequence = ++CurrentSequence;
    }

    /**
     * @brief 移除指定通道的请求条目
     * @param ChannelName 来源通道标识
     * @return 存在且成功移除返回 true，不存在返回 false
     */
    bool ClearChannel(const FName& ChannelName) {
        return Channels.Remove(ChannelName) > 0;
    }

    /**
     * @brief 检查指定通道当前是否处于激活状态
     * @param ChannelName 来源通道标识
     * @return 存在激活请求返回 true
     */
    [[nodiscard]] bool HasChannel(const FName& ChannelName) const noexcept {
        return Channels.Contains(ChannelName);
    }

    /**
     * @brief 获取指定通道当前的请求值
     * @param ChannelName 来源通道标识
     * @param OutValue 输出请求属性值
     * @return 通道存在返回 true
     */
    bool GetChannelValue(const FName& ChannelName, ValueType& OutValue) const {
        if (const FChannelRecord* Record = Channels.Find(ChannelName)) {
            OutValue = Record->Value;
            return true;
        }
        return false;
    }

    /**
     * @brief 计算并返回当前生效的最终仲裁值
     * @param DefaultValue 当所有通道为空时的默认回退基准值
     * @param Mode 仲裁模式（默认最高优先级抢占覆盖）
     * @return 综合仲裁后的最终属性值
     */
    [[nodiscard]] ValueType GetEffectiveValue(
        const ValueType& DefaultValue,
        EPriorityArbitrationMode Mode = EPriorityArbitrationMode::OverrideHighestPriority
    ) const {
        if (Channels.IsEmpty()) {
            return DefaultValue;
        }

        if (Mode == EPriorityArbitrationMode::OverrideHighestPriority) {
            const FWinningMatch Winner = FindWinner();
            return Winner.ValuePtr != nullptr ? *Winner.ValuePtr : DefaultValue;
        }

        if (Mode == EPriorityArbitrationMode::Additive) {
            ValueType Accum = DefaultValue;
            for (const auto& Pair : Channels) {
                Accum = Accum + Pair.Value.Value;
            }
            return Accum;
        }

        if (Mode == EPriorityArbitrationMode::Multiplicative) {
            ValueType Product = DefaultValue;
            for (const auto& Pair : Channels) {
                Product = Product * Pair.Value.Value;
            }
            return Product;
        }

        return DefaultValue;
    }

    /**
     * @brief 查询当前在最高优先级模式下胜出的通道名称
     * @return 胜出通道标识，若无有效通道则返回 NAME_None
     */
    [[nodiscard]] FName GetWinningChannel() const noexcept {
        return FindWinner().ChannelName;
    }

    /**
     * @brief 清空所有已注册的通道请求
     */
    void Reset() {
        Channels.Empty();
        CurrentSequence = 0;
    }

    /**
     * @brief 查询当前处于活跃状态的通道数量
     * @return 活跃通道数
     */
    [[nodiscard]] int32 Num() const noexcept {
        return Channels.Num();
    }

    /**
     * @brief 查询当前是否无任何活跃通道
     * @return 无活跃通道返回 true
     */
    [[nodiscard]] bool IsEmpty() const noexcept {
        return Channels.IsEmpty();
    }

private:
    struct FWinningMatch {
        FName ChannelName = NAME_None;
        const ValueType* ValuePtr = nullptr;
    };

    [[nodiscard]] FWinningMatch FindWinner() const noexcept {
        if (Channels.IsEmpty()) {
            return FWinningMatch();
        }

        int32 HighestPriority = TNumericLimits<int32>::Lowest();
        uint64 LatestSequence = 0;
        FWinningMatch Result;

        for (const auto& Pair : Channels) {
            const FChannelRecord& Rec = Pair.Value;
            const bool bIsHigherPriority = Rec.Priority > HighestPriority;
            const bool bIsTieBreak = (Rec.Priority == HighestPriority && Rec.Sequence > LatestSequence);
            if (!bIsHigherPriority && !bIsTieBreak) {
                continue;
            }
            HighestPriority = Rec.Priority;
            LatestSequence = Rec.Sequence;
            Result.ChannelName = Pair.Key;
            Result.ValuePtr = &Rec.Value;
        }

        return Result;
    }

private:
    TMap<FName, FChannelRecord> Channels;
    uint64 CurrentSequence = 0;
};
