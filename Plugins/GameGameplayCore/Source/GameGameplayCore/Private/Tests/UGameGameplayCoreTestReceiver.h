/**
 * @file UGameGameplayCoreTestReceiver.h
 * @brief 私有自动化测试用蓝图动态多播委托接收器声明
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"

#include "UGameGameplayCoreTestReceiver.generated.h"

/**
 * @brief 绑定 UGameStatComponent / UGameVitalComponent / UGameBuffComponent 蓝图动态多播委托的测试接收器
 */
UCLASS(NotBlueprintable, Hidden, Transient)
class UGameGameplayCoreTestReceiver : public UObject {
    GENERATED_BODY()

public:
    /**
     * @brief 接收属性变更动态多播委托
     * @param InStatTag 属性标签
     * @param InOldValue 旧值
     * @param InNewValue 新值
     * @param InMaxValue 上限值
     */
    UFUNCTION()
    void HandleStatChanged(FGameplayTag InStatTag, float InOldValue, float InNewValue, float InMaxValue) {
        ++StatChangedCount;
        LastStatTag = InStatTag;
        LastStatOld = InOldValue;
        LastStatNew = InNewValue;
        LastStatMax = InMaxValue;
    }

    /**
     * @brief 接收存活状态变更动态多播委托
     * @param bInIsAlive 最新存活状态
     * @param InVitalTag 主生命标签
     */
    UFUNCTION()
    void HandleVitalStateChanged(bool bInIsAlive, const FGameplayTag& InVitalTag) {
        ++VitalChangedCount;
        bLastVitalAlive = bInIsAlive;
        LastVitalTag = InVitalTag;
    }

    /**
     * @brief 接收 Buff 变更动态多播委托
     * @param InBuffTag Buff 标签
     * @param InStacks 当前聚合层数
     */
    UFUNCTION()
    void HandleBuffChanged(FGameplayTag InBuffTag, int32 InStacks) {
        ++BuffChangedCount;
        LastBuffChangedTag = InBuffTag;
        LastBuffStacks = InStacks;
    }

    /**
     * @brief 接收 Buff 完全移除动态多播委托
     * @param InBuffTag Buff 标签
     */
    UFUNCTION()
    void HandleBuffRemoved(FGameplayTag InBuffTag) {
        ++BuffRemovedCount;
        LastBuffRemovedTag = InBuffTag;
    }

public:
    int32 StatChangedCount = 0;
    FGameplayTag LastStatTag;
    float LastStatOld = 0.0f;
    float LastStatNew = 0.0f;
    float LastStatMax = 0.0f;

    int32 VitalChangedCount = 0;
    bool bLastVitalAlive = true;
    FGameplayTag LastVitalTag;

    int32 BuffChangedCount = 0;
    FGameplayTag LastBuffChangedTag;
    int32 LastBuffStacks = 0;

    int32 BuffRemovedCount = 0;
    FGameplayTag LastBuffRemovedTag;
};
