/**
 * @file FGameStatTypes.h
 * @brief 通用数值数据契约、强类型代际句柄、枚举与委托定义（纯数据与类型声明 · 零组件依赖）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Types/TGameHandle.h"

#include "FGameStatTypes.generated.h"

/**
 * @brief 属性修正器代际句柄幻影标签
 */
struct FStatModifierHandleTag {};

/**
 * @brief 属性修正器 64 位强类型代际句柄（32 位序号 + 32 位代际号，防异构混用与对象池重置后 ABA 误删）
 */
using FGameStatModifierHandle = TGameHandle<FStatModifierHandleTag>;

/**
 * @brief 通用数值配置与实时状态
 */
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameStatValue {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    float Current = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    float Max = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    float Min = 0.0f;

    /**
     * @brief 按给定当前值、上限与下限计算归一化百分比（钳制到 [0.0, 1.0]）
     * @param InCurrent 当前值
     * @param InMax 上限值
     * @param InMin 下限值
     * @return 钳制到 [0.0, 1.0] 的百分比，区间非法时返回 0.0f
     */
    [[nodiscard]] static FORCEINLINE float EvaluatePercent(float InCurrent, float InMax, float InMin) noexcept {
        const float Range = InMax - InMin;
        return (Range > 0.0f) ? FMath::Clamp((InCurrent - InMin) / Range, 0.0f, 1.0f) : 0.0f;
    }

    /**
     * @brief 获取归一化百分比（0~1）
     * @return 当前值在 [Min, Max] 区间的百分比，区间非法时返回 0.0f
     */
    [[nodiscard]] FORCEINLINE float GetPercent() const noexcept {
        return EvaluatePercent(Current, Max, Min);
    }

    /**
     * @brief 数值相等性比较（浮点近似）
     * @param Other 待比较的属性数值结构
     * @return 若当前值、上限与下限均近似相等返回 true
     */
    [[nodiscard]] bool operator==(const FGameStatValue& Other) const noexcept {
        return FMath::IsNearlyEqual(Current, Other.Current)
            && FMath::IsNearlyEqual(Max, Other.Max)
            && FMath::IsNearlyEqual(Min, Other.Min);
    }
};

/**
 * @brief 单趟演算产出的有效当前值与有效上限对
 */
struct FGameStatEffectivePair {
    float Current = 0.0f;
    float Max = 0.0f;
};

/**
 * @brief 用于网络复制的属性条目
 */
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameStatEntry {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Game|Stat")
    FGameplayTag Tag;

    UPROPERTY(BlueprintReadOnly, Category = "Game|Stat")
    FGameStatValue Value;

    FGameStatEntry() = default;

    /**
     * @brief 构造函数
     * @param InTag 属性标签
     * @param InValue 属性数值结构
     */
    FGameStatEntry(const FGameplayTag& InTag, const FGameStatValue& InValue)
        : Tag(InTag), Value(InValue) {}
};

/**
 * @brief 属性修正操作
 */
UENUM(BlueprintType)
enum class EGameStatModifierOp : uint8 {
    Add UMETA(DisplayName = "加算"),
    Multiply UMETA(DisplayName = "乘算"),
    Override UMETA(DisplayName = "覆写")
};

/**
 * @brief 单条属性修正
 */
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameStatModifier {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    EGameStatModifierOp Op = EGameStatModifierOp::Add;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    float Value = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    bool bApplyToCurrent = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    bool bApplyToMax = true;

    FGameStatModifier() = default;

    /**
     * @brief 构造函数
     * @param InValue 修正数值
     * @param InOp 修正操作
     * @param bInApplyToCurrent 是否作用于当前值
     * @param bInApplyToMax 是否作用于最大值
     */
    explicit FGameStatModifier(
        float InValue,
        EGameStatModifierOp InOp = EGameStatModifierOp::Add,
        bool bInApplyToCurrent = true,
        bool bInApplyToMax = true
    )
        : Op(InOp), Value(InValue), bApplyToCurrent(bInApplyToCurrent), bApplyToMax(bInApplyToMax) {}

    /**
     * @brief 修正规则相等性比较（数值采用浮点近似比较）
     * @param Other 待比较的属性修正器
     * @return 操作类型、作用范围与修正数值均等价返回 true
     */
    [[nodiscard]] bool operator==(const FGameStatModifier& Other) const noexcept {
        return Op == Other.Op
            && bApplyToCurrent == Other.bApplyToCurrent
            && bApplyToMax == Other.bApplyToMax
            && FMath::IsNearlyEqual(Value, Other.Value);
    }

    /**
     * @brief 构造乘算修正器
     * @param InMultiplier 乘算倍率
     * @param bInApplyToCurrent 是否作用于当前值
     * @param bInApplyToMax 是否作用于最大值
     * @return 乘算修正器实例
     */
    static FGameStatModifier MakeMultiplier(float InMultiplier, bool bInApplyToCurrent = true, bool bInApplyToMax = true) {
        return FGameStatModifier(InMultiplier, EGameStatModifierOp::Multiply, bInApplyToCurrent, bInApplyToMax);
    }
};

/**
 * @brief 单条已注册修正器（附带强类型代际句柄供精确更新与注销）
 */
struct FGameStatModifierEntry {
    FGameStatModifierHandle Handle;
    FGameStatModifier Modifier;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
    FOnGameStatChanged,
    FGameplayTag, StatTag,
    float, OldValue,
    float, NewValue,
    float, MaxValue
);

DECLARE_MULTICAST_DELEGATE_FourParams(
    FOnGameStatChangedNative,
    const FGameplayTag&,
    float,
    float,
    float
);
