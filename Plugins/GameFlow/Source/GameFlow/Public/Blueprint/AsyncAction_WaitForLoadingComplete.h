/**
 * @file AsyncAction_WaitForLoadingComplete.h
 * @brief 关卡流转与加载幕布解除蓝图异步等待动作节点声明
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"

#include "AsyncAction_WaitForLoadingComplete.generated.h"

class ULoadingSubsystem;

/**
 * @brief 关卡加载与就绪屏障全部解除时的蓝图执行引脚委托声明
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoadingScreenCompletedPin);

/**
 * @brief 等待被主动取消、上下文无效或子系统销毁中止时的蓝图执行引脚委托声明
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoadingScreenCancelledPin);

/**
 * @brief 异步等待关卡流转与全部就绪屏障解除的蓝图动作节点（幂等激活、重入安全与销毁自动清理）
 */
UCLASS()
class GAMEFLOW_API UAsyncAction_WaitForLoadingComplete : public UBlueprintAsyncActionBase {
    GENERATED_BODY()

public:
    /**
     * @brief 异步等待当前关卡加载与异步就绪屏障完全解除并在结束或中止时触发对应执行引脚
     * @param WorldContextObject 关联的世界或实例上下文对象
     * @return 蓝图异步操作实例
     */
    UFUNCTION(
        BlueprintCallable,
        meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"),
        Category = "GameFlow|Loading"
    )
    static UAsyncAction_WaitForLoadingComplete* WaitForLoadingComplete(const UObject* WorldContextObject);

public:
    /**
     * @brief 关卡加载与就绪屏障完全解除退出加载态时触发的蓝图执行引脚
     */
    UPROPERTY(BlueprintAssignable)
    FOnLoadingScreenCompletedPin OnCompleted;

    /**
     * @brief 上下文无效、主动调用 Cancel() 或子系统销毁中止等待时触发的蓝图执行引脚
     */
    UPROPERTY(BlueprintAssignable)
    FOnLoadingScreenCancelledPin OnCancelled;

public:
    /**
     * @brief 节点激活回调（幂等）：检查当前状态或挂钩子系统加载状态与销毁中止委托
     */
    void Activate() override;

    /**
     * @brief 终止异步操作并清理全部子系统委托监听句柄
     */
    void SetReadyToDestroy() override;

    /**
     * @brief UObject 销毁兜底回调：确保未经显式取消回收时仍安全解绑全部委托
     */
    void BeginDestroy() override;

    /**
     * @brief 手动终止并清理该异步等待动作节点（触发 OnCancelled 引脚，支持激活前或回调内安全调用）
     */
    UFUNCTION(BlueprintCallable, Category = "GameFlow|Loading")
    void Cancel();

private:
    /**
     * @brief 响应子系统加载状态变动事件
     * @param bIsLoading 是否仍处于加载中
     * @param LevelName 关联目标关卡名称
     */
    void HandleLoadingStateChanged(bool bIsLoading, FName LevelName);

    /**
     * @brief 响应子系统销毁导致的加载中止事件
     */
    void HandleLoadingAborted();

    /**
     * @brief 标记终态、先解绑委托再广播 OnCompleted 并交回 GC 回收
     */
    void FinishCompleted();

    /**
     * @brief 标记终态、先解绑委托再广播 OnCancelled 并交回 GC 回收
     */
    void FinishCancelled();

    /**
     * @brief 从加载子系统解绑全部已登记的监听句柄
     */
    void UnbindSubsystemDelegates();

private:
    TWeakObjectPtr<ULoadingSubsystem> SubsystemWeak;
    FDelegateHandle StateListenerHandle;
    FDelegateHandle AbortListenerHandle;
    bool bActivated = false;
    bool bTerminated = false;
};
