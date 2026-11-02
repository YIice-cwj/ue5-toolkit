/**
 * @file GameFeedbackAdapter.h
 * @brief 战斗伤害契约转换为表现信号的适配器
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Damage/FGameDamageContext.h"
#include "Damage/FGameDamageResult.h"
#include "Feedback/GameFeedbackSignal.h"

#include "GameFeedbackAdapter.generated.h"

/**
 * @brief 战斗伤害契约转换为表现信号的蓝图与原生函数库
 */
UCLASS()
class GAMEFEEDBACK_API UGameFeedbackAdapter : public UBlueprintFunctionLibrary {
    GENERATED_BODY()

public:
    /**
     * @brief 从伤害分发上下文与结算结果提取标准视听表现信号
     * @param InTarget 受击目标实体（显式传入）
     * @param Context 伤害上下文
     * @param Result 伤害结算结果
     * @return 转换后的视听表现信号
     */
    UFUNCTION(BlueprintPure, Category = "GameFeedback|Adapter")
    static FGameFeedbackSignal MakeSignalFromDamage(
        AActor* InTarget,
        const FGameDamageContext& Context,
        const FGameDamageResult& Result);
};

