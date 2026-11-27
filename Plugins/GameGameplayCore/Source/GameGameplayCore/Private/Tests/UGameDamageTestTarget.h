/**
 * @file UGameDamageTestTarget.h
 * @brief 私有自动化测试用 IDamageable 受击目标声明
 */

#pragma once

#include "CoreMinimal.h"
#include "Damage/FGameDamageContext.h"
#include "Damage/FGameDamageResult.h"
#include "Damage/IDamageable.h"
#include "UObject/Object.h"

#include "UGameDamageTestTarget.generated.h"

/**
 * @brief 私有自动化测试用受击目标（实现 80% 减伤结算并注入反馈标签与动态数值）
 */
UCLASS(NotBlueprintable, Hidden, Transient)
class UGameDamageTestTarget : public UObject, public IDamageable {
    GENERATED_BODY()

public:
    /**
     * @brief 响应伤害分发测试实现
     * @param Context 伤害分发上下文
     * @return 打击反馈结果
     */
    FGameDamageResult ApplyDamageContext_Implementation(const FGameDamageContext& Context) override;
};
