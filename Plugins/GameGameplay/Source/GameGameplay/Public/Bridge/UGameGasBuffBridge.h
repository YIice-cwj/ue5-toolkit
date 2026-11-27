/**
 * @file UGameGasBuffBridge.h
 * @brief GAS Buff 桥接（把 GameplayEffect 作为 Buff 载体的统一入口）
 *
 * 设计思路：
 * 非 GAS 项目使用 GameGameplayCore 的 UGameBuffComponent；已接入 GAS 的项目可直接用 GameplayEffect
 * 承载 Buff（时长、叠层、标签、属性修改均由 GAS 原生完成），本桥接层只收敛“上下文构建 + 施加 + 移除 + 查询”
 * 四步样板代码，支持同体与异体（SourceASC -> TargetASC）施加，不引入任何具体 Buff 类型与业务数值。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "UGameGasBuffBridge.generated.h"

class UAbilitySystemComponent;
class UGameplayEffect;

/**
 * @brief GAS Buff 桥接工具（纯静态函数，无状态）
 */
UCLASS()
class GAMEGAMEPLAY_API UGameGasBuffBridge : public UBlueprintFunctionLibrary {
    GENERATED_BODY()

public:
    /**
     * @brief 以 GameplayEffect 为载体施加 Buff（支持指定施法者 ASC，未指定时由目标 ASC 自施加；传入已失效的非空施法者将拒绝施加）
     * @param InAbilitySystemComponent 目标 ASC
     * @param InBuffEffectClass Buff 使用的 GameplayEffect 类
     * @param InLevel 效果等级
     * @param InSourceObject 施加来源对象（用于上下文追溯，可为空）
     * @param InSourceASC 施法者 ASC（nullptr 时回退至目标 ASC 自施加；非空但无效时拒绝施加）
     * @return 激活效果句柄，失败返回无效句柄
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    static FActiveGameplayEffectHandle ApplyBuffEffect(UAbilitySystemComponent* InAbilitySystemComponent, TSubclassOf<UGameplayEffect> InBuffEffectClass, int32 InLevel = 1, UObject* InSourceObject = nullptr, UAbilitySystemComponent* InSourceASC = nullptr);

    /**
     * @brief 移除指定激活的 Buff 效果
     * @param InAbilitySystemComponent 目标 ASC
     * @param InHandle 激活效果句柄
     * @param StacksToRemove 移除层数（-1 表示移除全部层数）
     * @return 是否移除成功
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    static bool RemoveBuffEffect(UAbilitySystemComponent* InAbilitySystemComponent, FActiveGameplayEffectHandle InHandle, int32 StacksToRemove = -1);

    /**
     * @brief 查询指定 Buff 效果是否仍处于激活状态
     * @param InAbilitySystemComponent 目标 ASC
     * @param InHandle 激活效果句柄
     * @return 仍激活返回 true
     */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] static bool IsBuffEffectActive(const UAbilitySystemComponent* InAbilitySystemComponent, FActiveGameplayEffectHandle InHandle);

    /**
     * @brief 获取指定 Buff 效果的当前叠层数
     * @param InAbilitySystemComponent 目标 ASC
     * @param InHandle 激活效果句柄
     * @return 当前叠层数，未激活返回 0
     */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] static int32 GetBuffStackCount(const UAbilitySystemComponent* InAbilitySystemComponent, FActiveGameplayEffectHandle InHandle);

    /**
     * @brief 获取指定 Buff 效果的剩余持续时长（秒）
     * @param InAbilitySystemComponent 目标 ASC
     * @param InHandle 激活效果句柄
     * @return 剩余持续时长（秒）；无限时长恒返回 -1.0f，未激活或有限时长且无 World 返回 0.0f
     */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] static float GetBuffRemainingTime(const UAbilitySystemComponent* InAbilitySystemComponent, FActiveGameplayEffectHandle InHandle);

    /**
     * @brief 获取指定 Buff 效果的总持续时长（秒）
     * @param InAbilitySystemComponent 目标 ASC
     * @param InHandle 激活效果句柄
     * @return 总持续时长，无限时长返回 -1.0f，未激活返回 0.0f
     */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] static float GetBuffDuration(const UAbilitySystemComponent* InAbilitySystemComponent, FActiveGameplayEffectHandle InHandle);
};
