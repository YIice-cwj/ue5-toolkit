/**
 * @file UGameAssetTestReceiver.h
 * @brief GameAssetCore 蓝图动态多播委托自动化测试接收器声明（私有内部测试头）
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "UGameAssetTestReceiver.generated.h"

class UAsyncAction_LoadGameAsset;
class UAsyncAction_LoadGameAssetsBatch;

/**
 * @brief 绑定 UAsyncAction_LoadGameAsset 与 UAsyncAction_LoadGameAssetsBatch 真实蓝图引脚的测试接收器
 */
UCLASS(NotBlueprintable, Hidden, Transient)
class UGameAssetTestReceiver : public UObject {
    GENERATED_BODY()

public:
    /**
     * @brief 接收单资产蓝图节点加载成功引脚事件
     * @param InLoadedAsset 加载完成的资产对象
     */
    UFUNCTION()
    void HandleAssetSuccess(UObject* InLoadedAsset) {
        ++SuccessCount;
        LastLoadedAsset = InLoadedAsset;
    }

    /**
     * @brief 接收单资产或批量蓝图节点加载失败引脚事件
     */
    UFUNCTION()
    void HandleAssetFailure() {
        ++FailureCount;
    }

    /**
     * @brief 接收单资产或批量蓝图节点进度更新引脚事件（支持配置在进度回调内重入取消）
     * @param InProgress 当前进度比例
     */
    UFUNCTION()
    void HandleAssetProgress(float InProgress);

    /**
     * @brief 接收批量蓝图节点加载成功引脚事件
     * @param InLoadedAssets 按原顺序对齐的已加载资产数组
     */
    UFUNCTION()
    void HandleBatchSuccess(const TArray<UObject*>& InLoadedAssets) {
        ++BatchSuccessCount;
        LastLoadedBatch = InLoadedAssets;
    }

public:
    UPROPERTY(Transient)
    TObjectPtr<UObject> LastLoadedAsset = nullptr;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UObject>> LastLoadedBatch;

    UPROPERTY(Transient)
    TObjectPtr<UAsyncAction_LoadGameAsset> SingleActionToCancelOnProgress = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UAsyncAction_LoadGameAssetsBatch> BatchActionToCancelOnProgress = nullptr;

    int32 SuccessCount = 0;
    int32 FailureCount = 0;
    int32 ProgressCount = 0;
    int32 BatchSuccessCount = 0;
    float LastProgress = -1.0f;
};
