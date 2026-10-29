/**
 * @file FGameStatTypes.h
 * @brief 通用数值数据契约、枚举与委托定义（纯数据与类型声明 · 零组件依赖）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Stat/IGameStatValueSource.h"

#include "FGameStatTypes.generated.h"

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
     * @brief 获取归一化百分比（0~1）
     * @return 当前值在 [Min, Max] 区间的百分比，区间非法时返回 0.0f
     */
    [[nodiscard]] FORCEINLINE float GetPercent() const noexcept {
        const float Range = Max - Min;
        return (Range > 0.0f) ? FMath::Clamp((Current - Min) / Range, 0.0f, 1.0f) : 0.0f;
    }
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
 * @brief 数值源模式
 */
UENUM(BlueprintType)
enum class EGameStatSourceMode : uint8 {
    Standalone UMETA(DisplayName = "自持模式（内部驱动）"),
    Proxy UMETA(DisplayName = "代理模式（外部源驱动）")
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
 * @brief 单条已注册修正器（附带句柄供精确注销）
 */
struct FGameStatModifierEntry {
    uint32 Handle = 0;
    FGameStatModifier Modifier;
};

/**
 * @brief 代理模式外部写操作类型
 */
UENUM(BlueprintType)
enum class EGameStatWriteOp : uint8 {
    Modify UMETA(DisplayName = "增量修改"),
    SetCurrent UMETA(DisplayName = "设置当前值"),
    SetMax UMETA(DisplayName = "设置上限")
};

DECLARE_DELEGATE_RetVal_OneParam(float, FGameStatQueryDelegate, const FGameplayTag&);
DECLARE_DELEGATE_RetVal_FourParams(bool, FGameStatWriteDelegate, const FGameplayTag&, EGameStatWriteOp, float, float&);

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
