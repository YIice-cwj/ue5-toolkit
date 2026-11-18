/**
 * @file ULoadingSubsystem.h
 * @brief 通用关卡流转与加载幕布子系统（支持异步业务就绪屏障令牌）
 *
 * 设计思路：在子系统 Initialize / Deinitialize 生命周期内自动挂钩与注销
 * FCoreUObjectDelegates::PreLoadMap / PostLoadMapWithWorld 委托，对外提供标准关卡跳转、加载态广播与异步就绪屏障投票机制。
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "System/FLoadingBarrierToken.h"
#include "System/GameFlowTypes.h"

#include "ULoadingSubsystem.generated.h"

/**
 * @brief 全局加载与关卡流转子系统
 *
 * 继承自 UGameInstanceSubsystem，作为跨关卡加载控制中枢。
 * 负责平滑关卡跳转调度、全屏遮罩/幕布广播、引擎底层地图加载事件监听，
 * 以及通过 FLoadingBarrierToken 汇聚异步业务子系统（对象池预热、资源预载、网络握手等）的就绪投票。
 */
UCLASS(BlueprintType)
class GAMEFLOW_API ULoadingSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    /**
     * @brief 静态工具方法：通过任意上下文对象获取当前加载子系统
     * @param WorldContextObject 世界上下文对象
     * @return 加载子系统指针，不存在返回 nullptr
     */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Loading", meta = (WorldContext = "WorldContextObject"))
    static ULoadingSubsystem* Get(const UObject* WorldContextObject);

    /**
     * @brief 加载状态变动多播委托（关卡加载开始及全部屏障解除结束时广播）
     */
    UPROPERTY(BlueprintAssignable, Category = "GameFlow|Loading")
    FOnLoadingStateChanged OnLoadingStateChanged;

    /**
     * @brief 加载状态变动原生 C++ 多播委托（无需 UObject 包装即可直接绑定 Lambda）
     */
    FOnLoadingStateChangedNative OnLoadingStateChangedNative;

    /**
     * @brief 加载综合进度变动动态多播委托（平滑百分比与当前步骤说明）
     */
    UPROPERTY(BlueprintAssignable, Category = "GameFlow|Loading")
    FOnLoadingProgressChanged OnLoadingProgressChanged;

    /**
     * @brief 加载综合进度变动原生 C++ 多播委托
     */
    FOnLoadingProgressChangedNative OnLoadingProgressChangedNative;

    /**
     * @brief 单次关卡流转完成时派发的耗时遥测报告原生多播委托
     */
    FOnLoadingTelemetryReportNative OnLoadingTelemetryReportNative;

    /**
     * @brief 子系统初始化生命周期回调：挂钩引擎 PreLoadMap 与 PostLoadMapWithWorld 委托
     * @param Collection 子系统管理器集合
     */
    void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * @brief 子系统销毁生命周期回调：注销引擎地图加载委托并清空活跃屏障
     */
    void Deinitialize() override;

    /**
     * @brief 携带加载幕布事件的标准关卡跳转接口
     * @param LevelName 目标关卡名称
     */
    UFUNCTION(BlueprintCallable, Category = "GameFlow|Loading")
    void OpenLevelWithLoading(const FName& LevelName);

    /**
     * @brief 申请异步加载就绪屏障栈上 RAII 令牌（零堆分配，支持移动语义）
     * @param BarrierName 屏障诊断名称（允许多个子系统使用相同名称，内部按唯一 ID 独立跟踪）
     * @param InWeight 进度加权权重（默认 1.0f）
     * @param InTimeoutSeconds 单屏障超时秒数（默认 0.0f 表示跟随子系统全局看门狗配置）
     * @return 栈上 RAII 屏障令牌，离开作用域析构或显式调用 Release() 时自动解除该票屏障
     */
    [[nodiscard]] FLoadingBarrierToken AcquireBarrierToken(
        const FName& BarrierName,
        float InWeight = 1.0f,
        float InTimeoutSeconds = 0.0f
    );

    /**
     * @brief 申请异步加载就绪屏障令牌（在令牌全部释放前，即使地图加载完毕也将保持加载幕布不提前关闭）
     * @param BarrierName 屏障诊断名称（允许多个子系统使用相同名称，内部按唯一 ID 独立跟踪）
     * @param InWeight 进度加权权重（默认 1.0f）
     * @param InTimeoutSeconds 单屏障超时秒数（默认 0.0f 表示跟随子系统全局看门狗配置）
     * @return RAII 屏障共享引用，析构或显式调用 Release() 时自动解除该票屏障
     */
    [[nodiscard]] TSharedRef<FLoadingBarrierToken> AcquireLoadingBarrier(
        const FName& BarrierName,
        float InWeight = 1.0f,
        float InTimeoutSeconds = 0.0f
    );

    /**
     * @brief 蓝图专用：申请异步就绪屏障并返回 64 位整数标识句柄
     * @param BarrierName 屏障诊断名称
     * @param InWeight 进度加权权重（默认 1.0f）
     * @param InTimeoutSeconds 单屏障超时秒数（默认 0.0f 表示跟随全局看门狗）
     * @return 屏障句柄 ID（大于 0）
     */
    UFUNCTION(BlueprintCallable, Category = "GameFlow|Loading", meta = (DisplayName = "Acquire Loading Barrier"))
    int64 AcquireLoadingBarrierBp(FName BarrierName, float InWeight = 1.0f, float InTimeoutSeconds = 0.0f);

    /**
     * @brief 蓝图专用：释放指定的屏障句柄
     * @param BarrierId 屏障句柄 ID
     * @return 成功移除有效屏障返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameFlow|Loading", meta = (DisplayName = "Release Loading Barrier"))
    bool ReleaseLoadingBarrierBp(int64 BarrierId);

    /**
     * @brief 蓝图专用：上报指定屏障句柄的当前内部进度
     * @param BarrierId 屏障句柄 ID
     * @param Progress 进度百分比 (0.0f ~ 1.0f)
     * @param Description 步骤说明文案
     * @return 屏障有效且更新成功返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameFlow|Loading", meta = (DisplayName = "Report Barrier Progress"))
    bool ReportBarrierProgressBp(int64 BarrierId, float Progress, const FText& Description);

    /**
     * @brief 获取当前全局综合加载进度
     * @return 进度百分比 (0.0f ~ 1.0f)
     */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Loading")
    float GetLoadingProgress() const;

    /**
     * @brief 设置关卡加载看门狗全局超时秒数（<=0 表示关闭看门狗兜底）
     * @param InTimeoutSeconds 超时秒数
     */
    UFUNCTION(BlueprintCallable, Category = "GameFlow|Loading")
    void SetMaxLoadingTimeoutSeconds(float InTimeoutSeconds);

    /**
     * @brief 获取关卡加载看门狗全局超时秒数
     * @return 超时秒数
     */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Loading")
    float GetMaxLoadingTimeoutSeconds() const;

    /**
     * @brief 查询当前是否正处于关卡流转或等待就绪屏障状态中
     * @return 若正在加载或等待屏障返回 true，否则返回 false
     */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Loading")
    bool IsLoading() const;

    /**
     * @brief 获取当前正在加载的目标关卡名称
     * @return 目标关卡名称；未加载时返回 NAME_None
     */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Loading")
    FName GetTargetLevelName() const;

    /**
     * @brief 查询当前是否存在尚未释放的加载就绪屏障
     * @return 存在至少一个活跃屏障返回 true
     */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Loading")
    bool HasActiveBarriers() const;

    /**
     * @brief 获取当前尚未释放的加载就绪屏障数量
     * @return 活跃屏障数
     */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Loading")
    int32 GetActiveBarrierCount() const;

    /**
     * @brief 显式执行一次看门狗超时巡检（在主循环 Tick、手动调度或单测中直接触发）
     * @param CurrentTimeSeconds 自定义模拟当前时间戳（默认 0.0 表示使用平台高精度时钟）
     */
    UFUNCTION(BlueprintCallable, Category = "GameFlow|Loading")
    void CheckWatchdogTimeout(double CurrentTimeSeconds = 0.0);

private:
    friend class FLoadingBarrierToken;

    /**
     * @brief 内部释放指定 ID 的加载就绪屏障；若地图已加载完成且屏障全部归零则触发加载结束广播
     * @param BarrierId 屏障唯一递增 ID
     */
    void ReleaseLoadingBarrierInternal(uint64 BarrierId);

    /**
     * @brief 内部更新屏障进度并触发全局进度重算广播
     * @param BarrierId 屏障 ID
     * @param InProgress 进度百分比 (0.0f ~ 1.0f)
     * @param InDescription 当前阶段说明
     */
    void UpdateBarrierProgressInternal(uint64 BarrierId, float InProgress, const FText& InDescription);

    /**
     * @brief 重新计算加权综合加载进度并对外派发广播
     * @param InDescription 当前步骤说明文案
     */
    void RecalculateAndBroadcastProgress(const FText& InDescription);

    /**
     * @brief 启动加载看门狗定时器
     */
    void StartWatchdogTimer();

    /**
     * @brief 停止并清理加载看门狗定时器
     */
    void StopWatchdogTimer();

    /**
     * @brief 定时器驱动的看门狗巡检入口
     */
    void OnWatchdogTimerTick();

    /**
     * @brief 引擎开始加载地图委托回调
     * @param MapName 即将加载的目标地图路径
     */
    void OnPreLoadMap(const FString& MapName);

    /**
     * @brief 引擎完成加载地图委托回调
     * @param LoadedWorld 加载完成的新世界对象指针
     */
    void OnPostLoadMap(UWorld* LoadedWorld);

    /**
     * @brief 完成本次加载流转状态复位并对外广播结束事件
     */
    void FinishLoadingTransition();

private:
    struct FActiveBarrierInfo {
        FName Name = NAME_None;
        float Weight = 1.0f;
        float Progress = 0.0f;
        double StartTimeSeconds = 0.0;
        float TimeoutSeconds = 0.0f;
    };

    /**
     * @brief 当前是否正处于关卡加载中
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading", meta = (AllowPrivateAccess = "true"))
    bool bIsLoading = false;

    /**
     * @brief 当前加载目标关卡名称
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading", meta = (AllowPrivateAccess = "true"))
    FName TargetLevelName = NAME_None;

    /**
     * @brief 当前综合平滑加载进度 (0.0f ~ 1.0f)
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameFlow|Loading", meta = (AllowPrivateAccess = "true"))
    float CurrentLoadingProgress = 0.0f;

    TMap<uint64, FActiveBarrierInfo> ActiveBarriers;
    TArray<FLoadingBarrierTelemetry> FinishedBarriersTelemetry;
    uint64 NextBarrierId = 0ULL;
    bool bMapLoadCompletedPendingBarriers = false;

    float MaxLoadingTimeoutSeconds = 30.0f;
    FTimerHandle WatchdogTimerHandle;
    double LoadingStartTimestamp = 0.0;
    double MapLoadFinishedTimestamp = 0.0;
};
