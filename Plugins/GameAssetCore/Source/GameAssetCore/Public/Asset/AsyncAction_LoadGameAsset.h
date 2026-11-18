/**
 * @file AsyncAction_LoadGameAsset.h
 * @brief 软引用资产异步流式加载蓝图异步动作节点
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "UObject/SoftObjectPtr.h"

#include "Asset/FGameAssetLoadHandle.h"
#include "Asset/GameAssetTypes.h"

#include "AsyncAction_LoadGameAsset.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGameAssetLoadedPin, UObject*, LoadedAsset);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGameAssetProgressPin, float, Progress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGameAssetFailedPin);

DECLARE_MULTICAST_DELEGATE_OneParam(FGameAssetLoadedNativeDelegate, UObject*);
DECLARE_MULTICAST_DELEGATE_OneParam(FGameAssetProgressNativeDelegate, float);
DECLARE_MULTICAST_DELEGATE(FGameAssetFailedNativeDelegate);

/**
 * @brief 软引用资产异步流式加载蓝图异步动作节点（弱宿主自动防悬垂与进度通知）
 */
UCLASS()
class GAMEASSETCORE_API UAsyncAction_LoadGameAsset : public UBlueprintAsyncActionBase {
    GENERATED_BODY()

public:
    /**
     * @brief 异步加载软引用资产并在成功、失败或进度更新时触发对应蓝图执行引脚
     * @param WorldContextObject 关联的世界上下文或宿主对象（若宿主销毁则自动中断回调）
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
     * @brief 异步加载软引用类并在成功、失败或进度更新时触发对应蓝图执行引脚
     * @param WorldContextObject 关联的世界上下文或宿主对象（若宿主销毁则自动中断回调）
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

    /**
     * @brief 资产加载成功触发蓝图执行引脚
     */
    UPROPERTY(BlueprintAssignable)
    FGameAssetLoadedPin OnSuccess;

    /**
     * @brief 资产加载失败或路径不存在触发蓝图执行引脚
     */
    UPROPERTY(BlueprintAssignable)
    FGameAssetFailedPin OnFailure;

    /**
     * @brief 资产加载进度更新触发蓝图执行引脚
     */
    UPROPERTY(BlueprintAssignable)
    FGameAssetProgressPin OnProgress;

    /** 原生 C++ 成功通知多播委托（支持 Lambda 绑定） */
    FGameAssetLoadedNativeDelegate OnSuccessNative;

    /** 原生 C++ 失败通知多播委托（支持 Lambda 绑定） */
    FGameAssetFailedNativeDelegate OnFailureNative;

    /** 原生 C++ 进度更新通知多播委托（支持 Lambda 绑定） */
    FGameAssetProgressNativeDelegate OnProgressNative;

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
    TSoftObjectPtr<UObject> TargetAsset;
    EGameAssetPriority LoadPriority = EGameAssetPriority::Normal;
    FGameAssetLoadHandle ActiveHandle;
};
