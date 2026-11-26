/**
 * @file ULoadingTestReceiver.h
 * @brief GameFlow 蓝图动态多播委托与异步节点引脚私有自动化测试接收器声明
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/GameFlowTypes.h"
#include "UObject/Object.h"

#include "ULoadingTestReceiver.generated.h"

/**
 * @brief 绑定 ULoadingSubsystem 与 UAsyncAction_WaitForLoadingComplete 真实蓝图动态引脚的测试接收器
 */
UCLASS(NotBlueprintable, Hidden, Transient)
class ULoadingTestReceiver : public UObject {
    GENERATED_BODY()

public:
    /**
     * @brief 接收加载状态变动动态多播回调
     * @param bInIsLoading 是否处于加载态
     * @param InTargetLevelName 目标关卡名称
     */
    UFUNCTION()
    void OnLoadingStateChanged(bool bInIsLoading, FName InTargetLevelName) {
        bLastIsLoading = bInIsLoading;
        LastTargetLevel = InTargetLevelName;
        ++LoadingChangeCount;
    }

    /**
     * @brief 接收加载进度变动动态多播回调
     * @param InProgress 当前综合进度百分比
     * @param InStepDescription 当前步骤说明文案
     */
    UFUNCTION()
    void OnLoadingProgressChanged(float InProgress, const FText& InStepDescription) {
        LastProgress = InProgress;
        LastStepDescription = InStepDescription.ToString();
        ++ProgressChangeCount;
    }

    /**
     * @brief 接收单次关卡流转耗时遥测报告动态多播回调
     * @param InReport 耗时遥测报告
     */
    UFUNCTION()
    void OnLoadingTelemetryReport(const FLoadingTelemetryReport& InReport) {
        LastTelemetryReport = InReport;
        ++TelemetryReportCount;
    }

    /**
     * @brief 接收 UAsyncAction_WaitForLoadingComplete 完成执行引脚回调
     */
    UFUNCTION()
    void OnActionCompleted() {
        ++ActionCompletedCount;
        if (OnActionCompletedHook) {
            TFunction<void()> Hook = MoveTemp(OnActionCompletedHook);
            Hook();
        }
    }

    /**
     * @brief 接收 UAsyncAction_WaitForLoadingComplete 取消或中止执行引脚回调
     */
    UFUNCTION()
    void OnActionCancelled() {
        ++ActionCancelledCount;
    }

public:
    bool bLastIsLoading = false;
    FName LastTargetLevel = NAME_None;
    int32 LoadingChangeCount = 0;

    float LastProgress = -1.0f;
    FString LastStepDescription;
    int32 ProgressChangeCount = 0;

    FLoadingTelemetryReport LastTelemetryReport;
    int32 TelemetryReportCount = 0;

    int32 ActionCompletedCount = 0;
    int32 ActionCancelledCount = 0;
    TFunction<void()> OnActionCompletedHook;
};
