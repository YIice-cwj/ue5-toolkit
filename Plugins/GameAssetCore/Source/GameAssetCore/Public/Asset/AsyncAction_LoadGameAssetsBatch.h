/**
 * @file AsyncAction_LoadGameAssetsBatch.h
 * @brief 软引用资产批量异步流式加载蓝图异步动作节点
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "UObject/SoftObjectPtr.h"

#include "Asset/FGameAssetLoadHandle.h"
#include "Asset/GameAssetTypes.h"

#include "AsyncAction_LoadGameAssetsBatch.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGameAssetsBatchLoadedPin, const TArray<UObject*>&, LoadedAssets);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGameAssetsBatchProgressPin, float, Progress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGameAssetsBatchFailedPin);

DECLARE_MULTICAST_DELEGATE_OneParam(FGameAssetsBatchLoadedNativeDelegate, const TArray<UObject*>&);
DECLARE_MULTICAST_DELEGATE_OneParam(FGameAssetsBatchProgressNativeDelegate, float);
DECLARE_MULTICAST_DELEGATE(FGameAssetsBatchFailedNativeDelegate);

/**
 * @brief 软引用资产批量异步流式加载蓝图异步动作节点（弱宿主自动防悬垂与进度通知）
 */
UCLASS()
class GAMEASSETCORE_API UAsyncAction_LoadGameAssetsBatch : public UBlueprintAsyncActionBase {
    GENERATED_BODY()

public:
    /**
     * @brief 批量异步加载一组软引用资产并在全部成功、失败或进度更新时触发对应蓝图执行引脚
     * @param WorldContextObject 关联的世界上下文或宿主对象（若宿主销毁则自动中断回调）
     * @param Assets 目标资产软引用数组
     * @param Priority 业务加载优先级（默认 Normal）
     * @return 蓝图异步操作实例
     */
    UFUNCTION(
        BlueprintCallable,
        meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"),
        Category = "GameAssetCore|AsyncLoad"
    )
    static UAsyncAction_LoadGameAssetsBatch* LoadGameAssetsBatchAsync(
        UObject* WorldContextObject,
        const TArray<TSoftObjectPtr<UObject>>& Assets,
        EGameAssetPriority Priority = EGameAssetPriority::Normal
    );

    /**
     * @brief 资产全部加载成功触发蓝图执行引脚（按原输入顺序返回已解析对象数组）
     */
    UPROPERTY(BlueprintAssignable)
    FGameAssetsBatchLoadedPin OnSuccess;

    /**
     * @brief 资产加载失败、包含无效路径或宿主失效触发蓝图执行引脚
     */
    UPROPERTY(BlueprintAssignable)
    FGameAssetsBatchFailedPin OnFailure;

    /**
     * @brief 资产批次加载进度更新触发蓝图执行引脚
     */
    UPROPERTY(BlueprintAssignable)
    FGameAssetsBatchProgressPin OnProgress;

    /** 原生 C++ 成功通知多播委托（支持 Lambda 绑定） */
    FGameAssetsBatchLoadedNativeDelegate OnSuccessNative;

    /** 原生 C++ 失败通知多播委托（支持 Lambda 绑定） */
    FGameAssetsBatchFailedNativeDelegate OnFailureNative;

    /** 原生 C++ 进度更新通知多播委托（支持 Lambda 绑定） */
    FGameAssetsBatchProgressNativeDelegate OnProgressNative;

    /**
     * @brief 节点激活回调：启动流式加载并建立生命周期监听
     */
    void Activate() override;

    /**
     * @brief 终止异步操作并清理流式加载句柄与代理
     */
    void SetReadyToDestroy() override;

    /**
     * @brief 手动终止加载任务（可供蓝图逻辑主动调用）
     */
    UFUNCTION(BlueprintCallable, Category = "GameAssetCore|AsyncLoad")
    void Cancel();

private:
    void HandleLoadCompleted();

private:
    TWeakObjectPtr<UObject> WorldContext;
    TArray<TSoftObjectPtr<UObject>> TargetAssets;
    EGameAssetPriority LoadPriority = EGameAssetPriority::Normal;
    FGameAssetLoadHandle ActiveHandle;
};
