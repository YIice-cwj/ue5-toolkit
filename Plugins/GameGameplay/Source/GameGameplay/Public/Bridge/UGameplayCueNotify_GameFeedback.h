/**
 * @file UGameplayCueNotify_GameFeedback.h
 * @brief 将 GAS GameplayCue 统一收敛至 GameFeedback 中枢的通用桥接器
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"

#include "UGameplayCueNotify_GameFeedback.generated.h"

/**
 * @brief GAS GameplayCue 桥接器（将 Cue 事件转换为 FGameFeedbackSignal 派发给表现中枢）
 */
UCLASS()
class GAMEGAMEPLAY_API UGameplayCueNotify_GameFeedback : public UGameplayCueNotify_Static {
    GENERATED_BODY()

public:
    UGameplayCueNotify_GameFeedback() = default;

    /**
     * @brief 触发 GameplayCue 执行回调
     * @param MyTarget 目标实体
     * @param Parameters Cue 携带的参数上下文
     * @return 执行是否成功
     */
    bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;
};

