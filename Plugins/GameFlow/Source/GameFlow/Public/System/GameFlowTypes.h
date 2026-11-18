/**
 * @file GameFlowTypes.h
 * @brief 关卡流转与加载子系统核心数据结构与委托定义
 */

#pragma once

#include "CoreMinimal.h"

#include "GameFlowTypes.generated.h"

/**
 * @brief 关卡加载状态发生变动时的动态多播委托声明
 * @param bIsLoading 是否正在加载关卡
 * @param TargetLevelName 目标关卡名称
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLoadingStateChanged, bool, bIsLoading, FName, TargetLevelName);

/**
 * @brief 关卡加载状态变动原生 C++ 多播委托声明（支持 Lambda 与协程直连）
 * @param bIsLoading 是否正在加载关卡
 * @param TargetLevelName 目标关卡名称
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnLoadingStateChangedNative, bool, FName);

/**
 * @brief 加载进度变动动态多播委托（百分比与步骤描述）
 * @param Progress 综合进度百分比 (0.0f ~ 1.0f)
 * @param StepDescription 当前步骤说明
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLoadingProgressChanged, float, Progress, const FText&, StepDescription);

/**
 * @brief 加载进度变动原生 C++ 多播委托
 * @param Progress 综合进度百分比 (0.0f ~ 1.0f)
 * @param StepDescription 当前步骤说明
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnLoadingProgressChangedNative, float, const FText&);

/**
 * @brief 单个屏障生命周期耗时遥测记录
 */
USTRUCT(BlueprintType)
struct GAMEFLOW_API FLoadingBarrierTelemetry {
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading")
    FName BarrierName = NAME_None;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading")
    float DurationMs = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading")
    float Weight = 1.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading")
    bool bTimedOut = false;
};

/**
 * @brief 单次关卡流转完整耗时遥测报告
 */
USTRUCT(BlueprintType)
struct GAMEFLOW_API FLoadingTelemetryReport {
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading")
    FName LevelName = NAME_None;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading")
    float TotalDurationMs = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading")
    float MapLoadDurationMs = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading")
    TArray<FLoadingBarrierTelemetry> Barriers;
};

/**
 * @brief 单次关卡流转完成时派发的耗时遥测报告原生多播委托
 * @param Report 遥测分析报告
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnLoadingTelemetryReportNative, const FLoadingTelemetryReport&);
