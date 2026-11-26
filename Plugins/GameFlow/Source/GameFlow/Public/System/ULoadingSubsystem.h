/**
 * @file ULoadingSubsystem.h
 * @brief 通用关卡流转与加载幕布子系统（基于 GameCore TSlotMap 代际屏障与多 PIE 上下文隔离）
 *
 * 设计思路：在子系统 Initialize / Deinitialize 生命周期内自动挂钩与注销
 * FCoreUObjectDelegates::PreLoadMapWithContext / PostLoadMapWithWorld 委托，严格按 OwningGameInstance 隔离多 PIE 实例；
 * 对外提供标准关卡跳转、加载态与加权进度广播、耗时遥测报告、自动联动看门狗超时熔断及异步就绪屏障投票机制。
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/TSlotMap.h"
#include "Core/GameFlowTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "System/FLoadingBarrierToken.h"

#include "ULoadingSubsystem.generated.h"

struct FWorldContext;

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
     * @brief 静态工具方法：通过任意有效上下文对象获取当前加载子系统（支持 ULoadingSubsystem、UGameInstance、Outer 链及 UWorld）
     * @param WorldContextObject 世界或实例上下文对象
     * @return 加载子系统指针，不存在或对象已失效返回 nullptr
     */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Loading", meta = (WorldContext = "WorldContextObject"))
    static ULoadingSubsystem* Get(const UObject* WorldContextObject);

public:
    /**
     * @brief 加载状态变动动态多播委托（关卡加载开始及全部屏障解除结束时广播）
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
     * @brief 单次关卡流转完成时派发的耗时遥测报告动态多播委托
     */
    UPROPERTY(BlueprintAssignable, Category = "GameFlow|Loading")
    FOnLoadingTelemetryReport OnLoadingTelemetryReport;

    /**
     * @brief 单次关卡流转完成时派发的耗时遥测报告原生多播委托
     */
    FOnLoadingTelemetryReportNative OnLoadingTelemetryReportNative;

    /**
     * @brief 活跃关卡加载因加载子系统销毁而被迫中止时派发的原生多播委托（供协程等待器与蓝图节点区分正常完成与销毁中止）
     */
    FOnLoadingAbortedNative OnLoadingAbortedNative;

public:
    /**
     * @brief 子系统初始化生命周期回调：挂钩引擎 PreLoadMapWithContext 与 PostLoadMapWithWorld 委托
     * @param Collection 子系统管理器集合
     */
    void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * @brief 子系统销毁生命周期回调：注销引擎委托、中止在途等待者并清空所有活跃屏障与委托监听
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
     * @param BarrierName 屏障诊断名称（允许多个子系统使用相同名称，内部按代际句柄独立跟踪）
     * @param InWeight 进度加权权重（默认 1.0f；下限截断至 0.1f，非有限浮点值自动回落至 1.0f）
     * @param InTimeoutSeconds 单屏障超时秒数（默认 0.0f 表示不设单屏障独立超时，仅受全局看门狗约束；>0 时即使全局超时关闭亦独立触发熔断）
     * @return 栈上值语义 RAII 屏障令牌，离开作用域析构或显式调用 Release() 时自动解除该票屏障
     */
    [[nodiscard]] FLoadingBarrierToken AcquireBarrierToken(
        const FName& BarrierName,
        float InWeight = 1.0f,
        float InTimeoutSeconds = 0.0f
    );

    /**
     * @brief 蓝图专用：申请异步就绪屏障并返回 64 位代际打包整数句柄
     * @param BarrierName 屏障诊断名称
     * @param InWeight 进度加权权重（默认 1.0f；下限截断至 0.1f，非有限浮点值自动回落至 1.0f）
     * @param InTimeoutSeconds 单屏障超时秒数（默认 0.0f 表示不设单屏障独立超时；>0 时独立启用看门狗巡检）
     * @return 64 位非零代际打包句柄
     */
    UFUNCTION(BlueprintCallable, Category = "GameFlow|Loading", meta = (DisplayName = "Acquire Loading Barrier"))
    int64 AcquireLoadingBarrierBp(FName BarrierName, float InWeight = 1.0f, float InTimeoutSeconds = 0.0f);

    /**
     * @brief 蓝图专用：释放指定的屏障句柄
     * @param BarrierId 屏障句柄 ID
     * @return 句柄有效且成功移除返回 true，无效或已过期返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameFlow|Loading", meta = (DisplayName = "Release Loading Barrier"))
    bool ReleaseLoadingBarrierBp(int64 BarrierId);

    /**
     * @brief 蓝图专用：上报指定屏障句柄的当前内部进度
     * @param BarrierId 屏障句柄 ID
     * @param Progress 进度百分比（0.0f ~ 1.0f；非有限浮点输入将被拒绝并返回 false）
     * @param Description 步骤说明文案
     * @return 屏障活跃且进度合法更新成功返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameFlow|Loading", meta = (DisplayName = "Report Barrier Progress"))
    bool ReportBarrierProgressBp(int64 BarrierId, float Progress, const FText& Description);

    /**
     * @brief 设置关卡加载看门狗全局超时秒数（<=0 或非有限值表示关闭全局超时，实时联动看门狗定时器启停）
     * @param InTimeoutSeconds 超时秒数
     */
    UFUNCTION(BlueprintCallable, Category = "GameFlow|Loading")
    void SetMaxLoadingTimeoutSeconds(float InTimeoutSeconds);

    /**
     * @brief 使用平台高精度时钟执行一次看门狗超时巡检
     */
    void CheckWatchdogTimeout();

    /**
     * @brief 使用显式时间戳执行一次看门狗超时巡检（供确定性单测或自定义时钟调度器调用）
     * @param CurrentTimeSeconds 显式传入的当前时间戳（秒，须为有限正数）
     */
    void CheckWatchdogTimeoutAt(double CurrentTimeSeconds);

    /**
     * @brief 查询当前是否正处于关卡流转或等待就绪屏障状态中
     * @return 若正在加载或等待屏障返回 true，否则返回 false
     */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Loading")
    bool IsLoading() const noexcept;

    /**
     * @brief 获取当前关卡流转精细阶段枚举（Idle / LoadingMap / WaitingBarriers）
     * @return 当前加载阶段
     */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Loading")
    ELoadingTransitionPhase GetLoadingPhase() const noexcept;

    /**
     * @brief 获取当前正在加载的目标关卡名称
     * @return 目标关卡名称；未加载时返回 NAME_None
     */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Loading")
    FName GetTargetLevelName() const noexcept;

    /**
     * @brief 获取当前全局综合加载进度
     * @return 进度百分比 (0.0f ~ 1.0f)
     */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Loading")
    float GetLoadingProgress() const noexcept;

    /**
     * @brief 获取关卡加载看门狗全局超时秒数
     * @return 超时秒数（0.0f 表示关闭全局超时）
     */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Loading")
    float GetMaxLoadingTimeoutSeconds() const noexcept;

    /**
     * @brief 查询当前是否存在尚未释放的加载就绪屏障
     * @return 存在至少一个活跃屏障返回 true
     */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Loading")
    bool HasActiveBarriers() const noexcept;

    /**
     * @brief 获取当前尚未释放的加载就绪屏障数量
     * @return 活跃屏障数
     */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Loading")
    int32 GetActiveBarrierCount() const noexcept;

    /**
     * @brief 获取最近一次已完成关卡流转的耗时遥测报告只读引用
     * @return 最近一次遥测报告
     */
    UFUNCTION(BlueprintPure, Category = "GameFlow|Loading")
    const FLoadingTelemetryReport& GetLastTelemetryReport() const noexcept;

private:
    friend class FLoadingBarrierToken;

    struct FActiveBarrierInfo {
        FName Name = NAME_None;
        float Weight = 1.0f;
        float Progress = 0.0f;
        float TimeoutSeconds = 0.0f;
        double StartTimeSeconds = 0.0;
    };

    /**
     * @brief 内部 O(1) 查询指定代际句柄对应的屏障是否仍然活跃
     * @param BarrierHandle 屏障代际句柄
     * @return 活跃返回 true
     */
    [[nodiscard]] bool IsBarrierActiveInternal(FLoadingBarrierHandle BarrierHandle) const noexcept;

    /**
     * @brief 内部登记新屏障至代际槽位表并按需联动看门狗与进度广播
     * @param BarrierName 屏障诊断名称
     * @param InWeight 原始权重
     * @param InTimeoutSeconds 原始单屏障超时秒数
     * @return 分配的强类型代际句柄
     */
    [[nodiscard]] FLoadingBarrierHandle RegisterBarrierInternal(
        const FName& BarrierName,
        float InWeight,
        float InTimeoutSeconds
    );

    /**
     * @brief 内部释放指定代际句柄的加载就绪屏障；若地图已加载完成且屏障归零则触发加载结束
     * @param BarrierHandle 屏障代际句柄
     * @return 句柄有效且成功移除返回 true
     */
    bool ReleaseLoadingBarrierInternal(FLoadingBarrierHandle BarrierHandle);

    /**
     * @brief 内部更新屏障进度并触发全局进度重算广播
     * @param BarrierHandle 屏障代际句柄
     * @param InProgress 原始进度百分比
     * @param InDescription 当前阶段说明
     * @param OutClampedProgress 输出校验截断后的有效进度值
     * @return 屏障活跃且输入进度合法返回 true
     */
    bool UpdateBarrierProgressInternal(
        FLoadingBarrierHandle BarrierHandle,
        float InProgress,
        const FText& InDescription,
        float& OutClampedProgress
    );

    /**
     * @brief 进入新一轮关卡流转：重置进度与遥测、联动看门狗并对外广播开始事件
     * @param InLevelName 目标关卡名称
     */
    void BeginLoadingTransition(const FName& InLevelName);

    /**
     * @brief 完成本次加载流转状态复位、生成遥测报告并对外广播结束事件
     * @param bTimedOut 是否因全局看门狗超时强制结束
     * @param CompletionTimestamp 结束时间戳（秒）
     */
    void FinishLoadingTransition(bool bTimedOut, double CompletionTimestamp);

    /**
     * @brief 基于 O(1) 已完成权重缓存与密集槽位表重算加权综合进度并按需派发广播
     * @param InDescription 当前步骤说明文案
     */
    void RecalculateAndBroadcastProgress(const FText& InDescription);

    /**
     * @brief 将已结束或超时的屏障写入本轮遥测明细并增量累计已完成权重
     * @param Info 屏障元数据快照
     * @param NowSeconds 当前时间戳（秒）
     * @param bTimedOut 是否超时熔断
     */
    void RecordBarrierTelemetry(const FActiveBarrierInfo& Info, double NowSeconds, bool bTimedOut);

    /**
     * @brief 处理全局看门狗总超时：批量记录超时屏障、清空槽位表并强制结束本轮加载
     * @param NowSeconds 当前时间戳（秒）
     */
    void HandleGlobalWatchdogTimeout(double NowSeconds);

    /**
     * @brief 巡检并精准熔断超出各自单屏障阈值的僵尸屏障（零堆分配内联缓冲）
     * @param NowSeconds 当前时间戳（秒）
     */
    void SweepTimedOutBarriers(double NowSeconds);

    /**
     * @brief 判定当前加载态与超时配置下看门狗定时器是否应当处于运行状态
     * @return 应当运行返回 true
     */
    [[nodiscard]] bool ShouldRunWatchdogTimer() const noexcept;

    /**
     * @brief 按 ShouldRunWatchdogTimer() 结果幂等启停加载看门狗定时器
     */
    void SyncWatchdogTimer();

    /**
     * @brief 停止并清理加载看门狗定时器
     */
    void StopWatchdogTimer();

    /**
     * @brief 定时器驱动的看门狗巡检入口
     */
    void OnWatchdogTimerTick();

    /**
     * @brief 引擎开始加载地图上下文委托回调（严格按 OwningGameInstance 隔离多 PIE 实例）
     * @param WorldContext 触发地图加载的引擎世界上下文
     * @param MapName 即将加载的目标地图路径
     */
    void OnPreLoadMapWithContext(const FWorldContext& WorldContext, const FString& MapName);

    /**
     * @brief 引擎完成加载地图委托回调（隔离异源 GameInstance 与无实例的编辑器预览世界）
     * @param LoadedWorld 加载完成的新世界对象指针（地图加载失败时引擎传入 nullptr）
     */
    void OnPostLoadMap(UWorld* LoadedWorld);

private:
    ELoadingTransitionPhase LoadingPhase = ELoadingTransitionPhase::Idle;
    FName TargetLevelName = NAME_None;
    float CurrentLoadingProgress = 0.0f;

    TSlotMap<FActiveBarrierInfo, FLoadingBarrierTag> ActiveBarriers;
    TArray<FLoadingBarrierTelemetry> FinishedBarriersTelemetry;
    FLoadingTelemetryReport LastTelemetryReport;
    float FinishedBarriersTotalWeight = 0.0f;
    int32 CustomTimeoutBarrierCount = 0;

    float MaxLoadingTimeoutSeconds = 30.0f;
    FTimerHandle WatchdogTimerHandle;
    double LoadingStartTimestamp = 0.0;
    double MapLoadFinishedTimestamp = 0.0;
    bool bIsFinishingTransition = false;
    FName PendingNextLevelName = NAME_None;
};
