/**
 * @file TPriorityArbitrator.h
 * @brief 多源优先级属性仲裁器模板（支持最高优先级覆盖、多源累加与链式乘算裁决）
 */

#pragma once

#include "CoreMinimal.h"
#include <concepts>

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
    /**
     * @brief 构造多源属性仲裁器实例并指定默认仲裁模式
     * @param InMode 仲裁模式（默认最高优先级抢占覆盖）
     */
    constexpr explicit TPriorityArbitrator(EPriorityArbitrationMode InMode = EPriorityArbitrationMode::OverrideHighestPriority) noexcept
        : ConfiguredMode(InMode) {
    }

    TPriorityArbitrator(const TPriorityArbitrator&) = delete;
    TPriorityArbitrator& operator=(const TPriorityArbitrator&) = delete;

    /**
     * @brief 移动构造函数（重置内部指针缓存，确保移动后无悬垂引用）
     * @param Other 源仲裁器引用
     */
    TPriorityArbitrator(TPriorityArbitrator&& Other) noexcept
        : Channels(MoveTemp(Other.Channels))
        , ConfiguredMode(Other.ConfiguredMode)
        , CurrentSequence(Other.CurrentSequence)
        , CachedWinnerChannel(Other.CachedWinnerChannel)
        , bWinnerCacheDirty(Other.bWinnerCacheDirty)
        , CachedAggregationBaseValue(MoveTemp(Other.CachedAggregationBaseValue))
        , CachedAggregationResult(MoveTemp(Other.CachedAggregationResult))
        , CachedAggregationMode(Other.CachedAggregationMode)
        , bAggregationCacheDirty(true) {
        SortedValuePtrsCache.Reset();
        Other.SortedValuePtrsCache.Reset();
        Other.bWinnerCacheDirty = true;
        Other.bAggregationCacheDirty = true;
    }

    /**
     * @brief 移动赋值运算符（重置内部指针缓存）
     * @param Other 源仲裁器引用
     * @return 自身引用
     */
    TPriorityArbitrator& operator=(TPriorityArbitrator&& Other) noexcept {
        if (this != &Other) {
            Channels = MoveTemp(Other.Channels);
            ConfiguredMode = Other.ConfiguredMode;
            CurrentSequence = Other.CurrentSequence;
            CachedWinnerChannel = Other.CachedWinnerChannel;
            bWinnerCacheDirty = Other.bWinnerCacheDirty;
            CachedAggregationBaseValue = MoveTemp(Other.CachedAggregationBaseValue);
            CachedAggregationResult = MoveTemp(Other.CachedAggregationResult);
            CachedAggregationMode = Other.CachedAggregationMode;
            bAggregationCacheDirty = true;
            SortedValuePtrsCache.Reset();
            Other.SortedValuePtrsCache.Reset();
            Other.bWinnerCacheDirty = true;
            Other.bAggregationCacheDirty = true;
        }
        return *this;
    }

    /**
     * @brief 获取当前配置的仲裁模式
     * @return 仲裁模式枚举值
     */
    [[nodiscard]] constexpr EPriorityArbitrationMode GetMode() const noexcept {
        return ConfiguredMode;
    }

    /**
     * @brief 更新仲裁模式并使对应缓存失效
     * @param InMode 新仲裁模式
     */
    void SetMode(EPriorityArbitrationMode InMode) noexcept {
        if (ConfiguredMode != InMode) {
            ConfiguredMode = InMode;
            bWinnerCacheDirty = true;
            bAggregationCacheDirty = true;
        }
    }

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
        bAggregationCacheDirty = true;
    }

    /**
     * @brief 移除指定通道的请求条目
     * @param ChannelName 来源通道标识
     * @return 存在且成功移除返回 true，不存在返回 false
     */
    bool ClearChannel(const FName& ChannelName) {
        if (Channels.Remove(ChannelName) > 0) {
            bWinnerCacheDirty = true;
            bAggregationCacheDirty = true;
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
     * @brief 获取指定通道当前的请求值（可选语义）
     * @param ChannelName 来源通道标识
     * @return 包含属性值的可选对象，通道不存在时返回未设置
     */
    [[nodiscard]] TOptional<ValueType> GetChannelValue(const FName& ChannelName) const {
        if (const FChannelRecord* Record = Channels.Find(ChannelName)) {
            return TOptional<ValueType>(Record->Value);
        }
        return TOptional<ValueType>();
    }

    /**
     * @brief 计算并返回当前生效的最终仲裁值（基于构造期配置或 SetMode 设定的仲裁模式计算）
     * @param DefaultValue 当所有通道为空时的默认回退值；在 Additive 与 Multiplicative 模式下同时作为累加/累乘的初始基准值
     * @return 综合仲裁后的最终属性值
     */
    [[nodiscard]] ValueType GetEffectiveValue(const ValueType& DefaultValue) const {
        return GetEffectiveValue(DefaultValue, ConfiguredMode);
    }

    /**
     * @brief 计算并返回当前生效的最终仲裁值（显式指定仲裁模式求值）
     * @param DefaultValue 当所有通道为空时的默认回退值；在 Additive 与 Multiplicative 模式下同时作为累加/累乘的初始基准值
     * @param Mode 显式指定的仲裁模式
     * @return 综合仲裁后的最终属性值
     */
    [[nodiscard]] ValueType GetEffectiveValue(
        const ValueType& DefaultValue,
        EPriorityArbitrationMode Mode
    ) const {
        if (Channels.IsEmpty()) {
            return DefaultValue;
        }

        if (Mode == EPriorityArbitrationMode::OverrideHighestPriority) {
            const FWinningMatch Winner = FindWinner();
            return Winner.ValuePtr != nullptr ? *Winner.ValuePtr : DefaultValue;
        }

        if (Mode == EPriorityArbitrationMode::Additive || Mode == EPriorityArbitrationMode::Multiplicative) {
            if constexpr (std::equality_comparable<ValueType>) {
                if (!bAggregationCacheDirty && CachedAggregationMode == Mode && CachedAggregationBaseValue == DefaultValue) {
                    return CachedAggregationResult;
                }
            }

            if (bAggregationCacheDirty) {
                RebuildAggregationPointers();
            }

            if (Mode == EPriorityArbitrationMode::Additive) {
                ValueType Accum = DefaultValue;
                for (const ValueType* ValPtr : SortedValuePtrsCache) {
                    Accum = Accum + *ValPtr;
                }
                if constexpr (std::equality_comparable<ValueType>) {
                    CachedAggregationBaseValue = DefaultValue;
                    CachedAggregationResult = Accum;
                    CachedAggregationMode = Mode;
                    bAggregationCacheDirty = false;
                }
                return Accum;
            }

            ValueType Product = DefaultValue;
            for (const ValueType* ValPtr : SortedValuePtrsCache) {
                Product = Product * (*ValPtr);
            }
            if constexpr (std::equality_comparable<ValueType>) {
                CachedAggregationBaseValue = DefaultValue;
                CachedAggregationResult = Product;
                CachedAggregationMode = Mode;
                bAggregationCacheDirty = false;
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
        SortedValuePtrsCache.Reset();
        bAggregationCacheDirty = false;
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

    void RebuildAggregationPointers() const {
        TArray<FName, TInlineAllocator<16>> SortedChannelNames;
        SortedChannelNames.Reserve(Channels.Num());
        for (const auto& Pair : Channels) {
            SortedChannelNames.Add(Pair.Key);
        }
        SortedChannelNames.Sort(FNameLexicalLess());

        SortedValuePtrsCache.Reset();
        SortedValuePtrsCache.Reserve(SortedChannelNames.Num());
        for (const FName& Key : SortedChannelNames) {
            SortedValuePtrsCache.Add(&Channels.FindChecked(Key).Value);
        }
    }

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
    EPriorityArbitrationMode ConfiguredMode = EPriorityArbitrationMode::OverrideHighestPriority;
    uint64 CurrentSequence = 0;
    mutable FName CachedWinnerChannel = NAME_None;
    mutable bool bWinnerCacheDirty = true;

    mutable TArray<const ValueType*, TInlineAllocator<16>> SortedValuePtrsCache;
    mutable ValueType CachedAggregationBaseValue{};
    mutable ValueType CachedAggregationResult{};
    mutable EPriorityArbitrationMode CachedAggregationMode = EPriorityArbitrationMode::OverrideHighestPriority;
    mutable bool bAggregationCacheDirty = true;
};
