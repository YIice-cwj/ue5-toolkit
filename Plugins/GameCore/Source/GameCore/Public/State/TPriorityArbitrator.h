/**
 * @file TPriorityArbitrator.h
 * @brief 多源优先级属性仲裁器模板（支持最高优先级覆盖、多源累加与链式乘算裁决）
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
     * @param InPriority 优先级数值（仅在 OverrideHighestPriority 模式下生效，数值越大优先级越高，默认 0）
     */
    void SetChannel(const FName& ChannelName, const ValueType& InValue, int32 InPriority = 0) {
        FChannelRecord& Record = Channels.FindOrAdd(ChannelName);
        Record.Value = InValue;
        Record.Priority = InPriority;
        Record.Sequence = ++CurrentSequence;
        bWinnerCacheDirty = true;
    }

    /**
     * @brief 移除指定通道的请求条目
     * @param ChannelName 来源通道标识
     * @return 存在且成功移除返回 true，不存在返回 false
     */
    bool ClearChannel(const FName& ChannelName) {
        if (Channels.Remove(ChannelName) > 0) {
            bWinnerCacheDirty = true;
            return true;
        }
        return false;
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
     * @brief 查找指定通道属性值的只读指针
     * @param ChannelName 来源通道标识
     * @return 存在时返回指向通道值的常量临时指针（后续 SetChannel/ClearChannel/Reset 可能导致该指针失效，严禁跨调用长期持有），不存在返回 nullptr
     */
    [[nodiscard]] const ValueType* FindChannelValue(const FName& ChannelName) const noexcept {
        if (const FChannelRecord* Record = Channels.Find(ChannelName)) {
            return &Record->Value;
        }
        return nullptr;
    }

    /**
     * @brief 获取指定通道当前的请求值（可选语义）
     * @param ChannelName 来源通道标识
     * @return 包含属性值的可选对象，通道不存在时返回未设置
     */
    [[nodiscard]] TOptional<ValueType> GetChannelValue(const FName& ChannelName) const {
        if (const ValueType* ValuePtr = FindChannelValue(ChannelName)) {
            return TOptional<ValueType>(*ValuePtr);
        }
        return TOptional<ValueType>();
    }

    /**
     * @brief 获取指定通道当前的请求值（历史传出参数重载，保持向下兼容）
     * @param ChannelName 来源通道标识
     * @param OutValue 输出请求属性值
     * @return 通道存在返回 true
     */
    bool GetChannelValue(const FName& ChannelName, ValueType& OutValue) const {
        if (const ValueType* ValuePtr = FindChannelValue(ChannelName)) {
            OutValue = *ValuePtr;
            return true;
        }
        return false;
    }

    /**
     * @brief 计算并返回当前生效的最终仲裁值（Additive/Multiplicative 按通道名字典序聚合以保证跨进程浮点确定性）
     * @param DefaultValue 当所有通道为空时的默认回退值；在 Additive 与 Multiplicative 模式下同时作为累加/累乘的初始基准值
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

        if (Mode == EPriorityArbitrationMode::Additive || Mode == EPriorityArbitrationMode::Multiplicative) {
            TArray<FName, TInlineAllocator<16>> SortedChannelNames;
            SortedChannelNames.Reserve(Channels.Num());
            for (const auto& Pair : Channels) {
                SortedChannelNames.Add(Pair.Key);
            }
            SortedChannelNames.Sort(FNameLexicalLess());

            if (Mode == EPriorityArbitrationMode::Additive) {
                ValueType Accum = DefaultValue;
                for (const FName& Key : SortedChannelNames) {
                    Accum = Accum + Channels.FindChecked(Key).Value;
                }
                return Accum;
            }

            ValueType Product = DefaultValue;
            for (const FName& Key : SortedChannelNames) {
                Product = Product * Channels.FindChecked(Key).Value;
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
        CachedWinnerChannel = NAME_None;
        bWinnerCacheDirty = false;
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
            CachedWinnerChannel = NAME_None;
            bWinnerCacheDirty = false;
            return FWinningMatch();
        }

        if (!bWinnerCacheDirty) {
            if (const FChannelRecord* CachedRec = Channels.Find(CachedWinnerChannel)) {
                return FWinningMatch{CachedWinnerChannel, &CachedRec->Value};
            }
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

        CachedWinnerChannel = Result.ChannelName;
        bWinnerCacheDirty = false;
        return Result;
    }

private:
    TMap<FName, FChannelRecord> Channels;
    uint64 CurrentSequence = 0;
    mutable FName CachedWinnerChannel = NAME_None;
    mutable bool bWinnerCacheDirty = true;
};
