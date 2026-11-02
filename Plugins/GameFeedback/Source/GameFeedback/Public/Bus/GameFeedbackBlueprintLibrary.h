/**
 * @file GameFeedbackBlueprintLibrary.h
 * @brief 打击感表现广播入口（调用方与实现之间的唯一契约）
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Feedback/GameFeedbackSignal.h"

#include "GameFeedbackBlueprintLibrary.generated.h"

class UGameFeedbackDefinition;

/**
 * @brief 蓝图与原生通用的表现广播函数库
 */
UCLASS()
class GAMEFEEDBACK_API UGameFeedbackBlueprintLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()

public:
    /**
     * @brief 广播一次打击表现信号（由总线上的各能力监听者自行消费）
     * @param WorldContextObject 世界上下文对象
     * @param Signal 纯表现信号
     */
    UFUNCTION(BlueprintCallable, Category = "GameFeedback")
    static void BroadcastFeedback(const UObject* WorldContextObject, const FGameFeedbackSignal& Signal);

    /**
     * @brief 以指定资产覆盖注册表匹配结果后广播一次表现信号
     * @param WorldContextObject 世界上下文对象
     * @param Signal 纯表现信号
     * @param Definition 指定的表现资产（仅本地监听者生效）
     */
    UFUNCTION(BlueprintCallable, Category = "GameFeedback")
    static void BroadcastFeedbackWithDefinition(const UObject* WorldContextObject, const FGameFeedbackSignal& Signal, const UGameFeedbackDefinition* Definition);

    /**
     * @brief 广播清除指定实体的表现状态
     * @param WorldContextObject 世界上下文对象
     * @param Target 待重置的目标实体
     */
    UFUNCTION(BlueprintCallable, Category = "GameFeedback")
    static void BroadcastClearFeedback(const UObject* WorldContextObject, AActor* Target);

    /**
     * @brief 将携带服务端最终 Definition 的网络回执回灌本地总线
     * @param WorldContextObject 世界上下文对象
     * @param Signal 还原后的表现信号
     * @param ResolvedDefinition 服务端最终 Definition
     * @param bSuppressed 本地预测已播放而需要抑制重复表现
     */
    static void BroadcastNetworkFeedback(
        const UObject* WorldContextObject,
        const FGameFeedbackSignal& Signal,
        const UGameFeedbackDefinition* ResolvedDefinition,
        bool bSuppressed);
};
