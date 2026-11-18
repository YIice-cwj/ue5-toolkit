/**
 * @file AsyncAction_WaitForLoadingComplete.h
 * @brief 关卡流转与加载幕布解除蓝图异步等待动作节点
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"

#include "AsyncAction_WaitForLoadingComplete.generated.h"

class ULoadingSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoadingScreenCompletedPin);
DECLARE_MULTICAST_DELEGATE(FOnLoadingScreenCompletedNative);

/**
 * @brief 异步等待关卡流转与全部就绪屏障解除的蓝图动作节点
 */
UCLASS()
class GAMEFLOW_API UAsyncAction_WaitForLoadingComplete : public UBlueprintAsyncActionBase {
    GENERATED_BODY()

public:
    /**
     * @brief 异步等待当前关卡加载与异步就绪屏障完全解除并在结束时触发执行引脚
     * @param WorldContextObject 关联的世界上下文对象
     * @return 蓝图异步操作实例
     */
    UFUNCTION(
        BlueprintCallable,
        meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"),
        Category = "GameFlow|Loading"
    )
    static UAsyncAction_WaitForLoadingComplete* WaitForLoadingComplete(const UObject* WorldContextObject);

    /**
     * @brief 关卡加载与就绪屏障完全解除退出加载态时触发的蓝图执行引脚
     */
    UPROPERTY(BlueprintAssignable)
    FOnLoadingScreenCompletedPin OnCompleted;

    /**
     * @brief 关卡加载与就绪屏障完全解除退出加载态时触发的原生 C++ 多播委托（支持直接绑定 Lambda）
     */
    FOnLoadingScreenCompletedNative OnCompletedNative;

    /**
     * @brief 节点激活回调：检查当前状态或挂钩子系统加载态变动委托
     */
    void Activate() override;

    /**
     * @brief 终止异步操作并清理委托监听句柄
     */
    void SetReadyToDestroy() override;

    /**
     * @brief 手动终止并清理该异步等待动作节点（供蓝图逻辑主动调用）
     */
    UFUNCTION(BlueprintCallable, Category = "GameFlow|Loading")
    void Cancel();

private:
    void HandleLoadingStateChanged(bool bIsLoading, FName LevelName);

private:
    TWeakObjectPtr<ULoadingSubsystem> SubsystemWeak;
    FDelegateHandle StateListenerHandle;
};
