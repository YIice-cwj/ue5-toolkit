/**
 * @file UGameAttributeMappingTestReceiver.h
 * @brief 自动化测试专用属性映射蓝图动态委托接收器
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "UGameAttributeMappingTestReceiver.generated.h"

/**
 * @brief 用于验证 UGameplayAttributeValueMapping 动态多播 OnValueChanged 路径的测试接收对象
 */
UCLASS(NotBlueprintable)
class UGameAttributeMappingTestReceiver : public UObject {
    GENERATED_BODY()

public:
    /**
     * @brief 蓝图动态多播接收回调
     * @param NewValue 变更后新值
     * @param OldValue 变更前旧值
     */
    UFUNCTION()
    void HandleMappedValueChanged(float NewValue, float OldValue) {
        LastNewValue = NewValue;
        LastOldValue = OldValue;
        ++CallCount;
    }

    float LastNewValue = 0.0f;
    float LastOldValue = 0.0f;
    int32 CallCount = 0;
};
