/**
 * @file GameFlowTypes.h
 * @brief 关卡流转与异步就绪屏障核心类型、强类型代际句柄、遥测结构与多播委托定义
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/TGameHandle.h"

#include "GameFlowTypes.generated.h"

/**
 * @brief 异步就绪屏障代际句柄幻影标签
 */
struct FLoadingBarrierTag {};

/**
 * @brief 异步就绪屏障 64 位强类型代际句柄（32 位槽位索引 + 32 位代际号，防过期复用与异构混用）
 */
using FLoadingBarrierHandle = TGameHandle<FLoadingBarrierTag>;

/**
 * @brief 关卡流转加载阶段枚举
 */
UENUM(BlueprintType)
enum class ELoadingTransitionPhase : uint8 {
    /**
     * @brief 空闲态：当前未处于关卡流转加载过程
     */
    Idle UMETA(DisplayName = "Idle"),

    /**
     * @brief 地图加载态：已启动关卡流转，等待引擎地图加载完成
     */
    LoadingMap UMETA(DisplayName = "Loading Map"),

    /**
     * @brief 等待屏障态：地图加载完毕，等待全部异步业务就绪屏障释放
     */
    WaitingBarriers UMETA(DisplayName = "Waiting Barriers")
};

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
 * @brief 加载进度变动动态多播委托（综合百分比与当前步骤说明）
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
 * @brief 活跃加载流转因加载子系统销毁而被迫中止时派发的原生多播委托
 */
DECLARE_MULTICAST_DELEGATE(FOnLoadingAbortedNative);

/**
 * @brief 单个异步就绪屏障的生命周期耗时遥测记录
 */
USTRUCT(BlueprintType)
struct GAMEFLOW_API FLoadingBarrierTelemetry {
    GENERATED_BODY()

    /**
     * @brief 屏障申请时登记的诊断名称
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading")
    FName BarrierName = NAME_None;

    /**
     * @brief 屏障从申请到释放或超时的总耗时（毫秒）
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading")
    float DurationMs = 0.0f;

    /**
     * @brief 屏障参与全局进度加权计算的标准化权重
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading")
    float Weight = 1.0f;

    /**
     * @brief 该屏障是否因看门狗超时而被强制熔断解除
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading")
    bool bTimedOut = false;
};

/**
 * @brief 单次关卡流转完整耗时与屏障明细遥测报告
 */
USTRUCT(BlueprintType)
struct GAMEFLOW_API FLoadingTelemetryReport {
    GENERATED_BODY()

    /**
     * @brief 本次流转的目标关卡名称
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading")
    FName LevelName = NAME_None;

    /**
     * @brief 本次流转总耗时（毫秒）
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading")
    float TotalDurationMs = 0.0f;

    /**
     * @brief 地图载入阶段耗时（毫秒；若地图阶段即触发全局超时则等于总耗时）
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading")
    float MapLoadDurationMs = 0.0f;

    /**
     * @brief 本次关卡流转是否因全局看门狗超时而强制结束
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading")
    bool bTimedOut = false;

    /**
     * @brief 本次流转期间完成的全部异步屏障遥测明细
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading")
    TArray<FLoadingBarrierTelemetry> Barriers;
};

/**
 * @brief 单次关卡流转完成时派发的耗时遥测报告动态多播委托
 * @param Report 遥测分析报告
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoadingTelemetryReport, const FLoadingTelemetryReport&, Report);

/**
 * @brief 单次关卡流转完成时派发的耗时遥测报告原生多播委托
 * @param Report 遥测分析报告
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnLoadingTelemetryReportNative, const FLoadingTelemetryReport&);
