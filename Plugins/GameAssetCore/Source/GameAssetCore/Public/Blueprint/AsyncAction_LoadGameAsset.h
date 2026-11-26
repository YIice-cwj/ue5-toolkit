/**
 * @file AsyncAction_LoadGameAsset.h
 * @brief 软引用单资产与类异步流式加载蓝图动作节点声明
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"

#include "Core/GameAssetTypes.h"
#include "Loader/FGameAssetLoadHandle.h"

#include "AsyncAction_LoadGameAsset.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGameAssetLoadedPin, UObject*, LoadedAsset);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGameAssetProgressPin, float, Progress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGameAssetFailedPin);

/**
 * @brief 软引用资产与类异步流式加载蓝图动作节点（复用 FGameAssetLoader，具备弱宿主保护与重入取消守卫）
 */
UCLASS()
class GAMEASSETCORE_API UAsyncAction_LoadGameAsset : public UBlueprintAsyncActionBase {
    GENERATED_BODY()

public:
    /**
     * @brief 节点激活回调（幂等）：启动底层异步流送并建立生命周期监听
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
     * @brief 主动终止当前异步加载任务（支持在 OnProgress 回调中安全重入调用）
     */
    UFUNCTION(BlueprintCallable, Category = "GameAssetCore|AsyncLoad")
    void Cancel();

public:
    /**
     * @brief 异步加载单个软引用资产并在成功、失败或进度更新时触发对应蓝图执行引脚
     * @param WorldContextObject 关联的世界上下文或宿主对象（若宿主销毁则自动中断回调并回收节点）
     * @param Asset 目标资产软引用
     * @param Priority 业务加载优先级（默认 Normal）
     * @return 蓝图异步操作实例
     */
    UFUNCTION(
        BlueprintCallable,
        meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"),
        Category = "GameAssetCore|AsyncLoad"
    )
    static UAsyncAction_LoadGameAsset* LoadGameAssetAsync(
        UObject* WorldContextObject,
        TSoftObjectPtr<UObject> Asset,
        EGameAssetPriority Priority = EGameAssetPriority::Normal
    );

    /**
     * @brief 异步加载单个软引用类（强制校验加载结果为有效 UClass）并在完成或失败时触发对应蓝图执行引脚
     * @param WorldContextObject 关联的世界上下文或宿主对象（若宿主销毁则自动中断回调并回收节点）
     * @param Class 目标类软引用
     * @param Priority 业务加载优先级（默认 Normal）
     * @return 蓝图异步操作实例
     */
    UFUNCTION(
        BlueprintCallable,
        meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"),
        Category = "GameAssetCore|AsyncLoad"
    )
    static UAsyncAction_LoadGameAsset* LoadGameClassAsync(
        UObject* WorldContextObject,
        TSoftClassPtr<UObject> Class,
        EGameAssetPriority Priority = EGameAssetPriority::Normal
    );

private:
    void BindToGameInstance(UObject* WorldContextObject);
    void HandleAssetLoaded(UObject* LoadedObject);
    void HandleProgressUpdated(float CurrentProgress);
    void BroadcastFailureAndDestroy();
    void ReleaseOrCancelHandle();

private:
    TWeakObjectPtr<UObject> WorldContext;
    FSoftObjectPath TargetPath;
    EGameAssetPriority LoadPriority = EGameAssetPriority::Normal;
    FGameAssetLoadHandle ActiveHandle;
    bool bRequireClass = false;
    bool bActivated = false;
    bool bTerminated = false;

public:
    /**
     * @brief 资产或类加载成功时触发的蓝图执行引脚
     */
    UPROPERTY(BlueprintAssignable)
    FGameAssetLoadedPin OnSuccess;

    /**
     * @brief 资产路径无效、加载失败或类型不匹配时触发的蓝图执行引脚
     */
    UPROPERTY(BlueprintAssignable)
    FGameAssetFailedPin OnFailure;

    /**
     * @brief 资产流式加载进度更新时触发的蓝图执行引脚
     */
    UPROPERTY(BlueprintAssignable)
    FGameAssetProgressPin OnProgress;
};
