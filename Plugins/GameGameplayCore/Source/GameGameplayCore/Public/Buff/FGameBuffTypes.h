/**
 * @file FGameBuffTypes.h
 * @brief Buff 核心数据契约、枚举与委托定义（纯数据类型 · 零组件依赖）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Stat/FGameStatTypes.h"

#include "FGameBuffTypes.generated.h"

/**
 * @brief 重复施加同 Tag Buff 时的叠层策略
 */
UENUM(BlueprintType)
enum class EGameBuffStackPolicy : uint8 {
    /**
     * @brief 刷新持续时长（层数加 1 直至上限，持续时间重置为 Spec 的初始时长）
     */
    RefreshDuration UMETA(DisplayName = "刷新持续时长"),

    /**
     * @brief 累加持续时长（层数加 1 直至上限，持续时间在现有剩余时间上追加初始时长）
     */
    AddDuration UMETA(DisplayName = "累加持续时长"),

    /**
     * @brief 独立实例（每次施加创建互不干扰的独立计时实例，层数各自独立计算）
     */
    Independent UMETA(DisplayName = "独立实例")
};

/**
 * @brief 单条属性修正声明（Buff 激活期间挂接到目标属性）
 */
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameStatModifierSpec {
    GENERATED_BODY()

    /**
     * @brief 目标属性标签（例如 Stat.Attack、Stat.Defense）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    FGameplayTag StatTag;

    /**
     * @brief 属性修饰器数据（加算、乘算或绝对覆写）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    FGameStatModifier Modifier;
};

/**
 * @brief 通用 Buff 描述（开发者自由构造，框架不预置任何具体 Buff）
 */
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameBuffSpec {
    GENERATED_BODY()

    /**
     * @brief Buff 身份标签（同一 Tag 视为同一种 Buff，用于叠层与查询）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    FGameplayTag BuffTag;

    /**
     * @brief 激活期间对外聚合的状态标签（如 Status.Stun、Status.Invulnerable）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    FGameplayTagContainer GrantedTags;

    /**
     * @brief 持续时长（秒；<= 0 表示永久生效）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    float Duration = 0.0f;

    /**
     * @brief 最大叠层数（<= 0 表示不限层数）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    int32 MaxStacks = 1;

    /**
     * @brief 重复施加时的叠层策略
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    EGameBuffStackPolicy StackPolicy = EGameBuffStackPolicy::RefreshDuration;

    /**
     * @brief 激活期间挂接到 UGameStatComponent 的属性修正器列表
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    TArray<FGameStatModifierSpec> Modifiers;
};

/**
 * @brief 已挂接的属性修正器记录（保留标签以支持精确回滚）
 */
struct FGameBuffAppliedModifier {
    /**
     * @brief 目标属性标签
     */
    FGameplayTag StatTag;

    /**
     * @brief 挂接在属性组件中的修饰器句柄
     */
    uint32 Handle = 0;
};

/**
 * @brief 动态多播委托：Buff 层数或状态发生变动
 * @param BuffTag 发生变动的 Buff 标签
 * @param Stacks 当前聚合层数
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGameBuffChanged, FGameplayTag, BuffTag, int32, Stacks);

/**
 * @brief 动态多播委托：Buff 实例全部移除
 * @param BuffTag 被移除的 Buff 标签
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameBuffRemoved, FGameplayTag, BuffTag);

/**
 * @brief 原生 C++ 多播委托：Buff 层数或状态发生变动
 * @param BuffTag 发生变动的 Buff 标签引用
 * @param Stacks 当前聚合层数
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnGameBuffChangedNative, const FGameplayTag&, int32);

/**
 * @brief 原生 C++ 多播委托：Buff 实例全部移除
 * @param BuffTag 被移除的 Buff 标签引用
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnGameBuffRemovedNative, const FGameplayTag&);
