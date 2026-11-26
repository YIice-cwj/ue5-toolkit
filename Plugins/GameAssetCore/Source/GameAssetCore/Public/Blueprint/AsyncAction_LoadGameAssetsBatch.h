/**
 * @file AsyncAction_LoadGameAssetsBatch.h
 * @brief 软引用资产批量异步流式加载蓝图动作节点声明
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "UObject/SoftObjectPtr.h"

#include "Core/GameAssetTypes.h"
#include "Loader/FGameAssetLoadHandle.h"

#include "AsyncAction_LoadGameAssetsBatch.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGameAssetsBatchLoadedPin, const TArray<UObject*>&, LoadedAssets);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGameAssetsBatchProgressPin, float, Progress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGameAssetsBatchFailedPin);

/**
 * @brief 软引用资产批量异步流式加载蓝图动作节点（复用 FGameAssetLoader 路径去重、弱宿主保护与重入取消守卫）
 */
UCLASS()
class GAMEASSETCORE_API UAsyncAction_LoadGameAssetsBatch : public UBlueprintAsyncActionBase {
    GENERATED_BODY()

public:
    /**
     * @brief 节点激活回调（幂等）：校验批次输入并启动底层去重流式加载
     */
    void Activate() override;

    /**
     * @brief 终止异步操作并按加载状态安全取消或释放底层流式句柄
     */
    void SetReadyToDestroy() override;

    /**
     * @brief UObject 销毁兜底回调：确保未经显式取消回收时亦安全释放底层句柄
     */
    void BeginDestroy() override;

    /**
     * @brief 主动终止当前批量异步加载任务（支持在 OnProgress 回调中安全重入调用）
     */
    UFUNCTION(BlueprintCallable, Category = "GameAssetCore|AsyncLoad")
    void Cancel();

public:
    /**
     * @brief 批量异步加载一组软引用资产并在全部成功、失败或进度更新时触发对应蓝图执行引脚
     * @param WorldContextObject 关联的世界上下文或宿主对象（若宿主销毁则自动中断回调并回收节点）
     * @param Assets 目标资产软引用数组（空数组或含空元素时触发 OnFailure）
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

private:
    void BindToGameInstance(UObject* WorldContextObject);
    void HandleBatchLoaded();
    void HandleProgressUpdated(float CurrentProgress);
    void BroadcastFailureAndDestroy();
    void ReleaseOrCancelHandle();

private:
    TWeakObjectPtr<UObject> WorldContext;
    TArray<TSoftObjectPtr<UObject>> TargetAssets;
    EGameAssetPriority LoadPriority = EGameAssetPriority::Normal;
    FGameAssetLoadHandle ActiveHandle;
    bool bActivated = false;
    bool bTerminated = false;

public:
    /**
     * @brief 全部资产加载成功时触发的蓝图执行引脚（按原输入顺序返回已解析对象数组）
     */
    UPROPERTY(BlueprintAssignable)
    FGameAssetsBatchLoadedPin OnSuccess;

    /**
     * @brief 批次为空、包含无效路径或任一资产加载失败时触发的蓝图执行引脚
     */
    UPROPERTY(BlueprintAssignable)
    FGameAssetsBatchFailedPin OnFailure;

    /**
     * @brief 资产批次流式加载进度更新时触发的蓝图执行引脚
     */
    UPROPERTY(BlueprintAssignable)
    FGameAssetsBatchProgressPin OnProgress;
};
