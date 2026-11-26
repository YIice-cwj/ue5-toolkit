/**
 * @file UGameAssetBlueprintLibrary.h
 * @brief 资产常驻保活与免阻塞状态查询蓝图函数库声明
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"

#include "UGameAssetBlueprintLibrary.generated.h"

/**
 * @brief 面向蓝图的 GameAssetCore 资产常驻保活与零阻塞驻留查询函数库
 */
UCLASS()
class GAMEASSETCORE_API UGameAssetBlueprintLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()

public:
    /**
     * @brief 检查指定软引用资产是否处于内存常驻保活状态
     * @param Asset 目标软引用资产
     * @return 处于保活状态返回 true
     */
    UFUNCTION(BlueprintPure, Category = "GameAssetCore|Pin")
    static bool IsAssetPinned(const TSoftObjectPtr<UObject>& Asset);

    /**
     * @brief 锁定指定软引用资产常驻内存（递增引用计数，防止被 GC 回收）
     * @param Asset 目标软引用资产
     * @return 锁定或递增计数成功返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameAssetCore|Pin")
    static bool PinAsset(const TSoftObjectPtr<UObject>& Asset);

    /**
     * @brief 解除指定软引用资产的内存常驻锁定（递减引用计数，归零时释放）
     * @param Asset 目标软引用资产
     * @return 递减或释放成功返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameAssetCore|Pin")
    static bool UnpinAsset(const TSoftObjectPtr<UObject>& Asset);

    /**
     * @brief 批量锁定一组软引用资产常驻内存
     * @param Assets 目标软引用资产数组
     * @return 成功增加常驻锁定的资产数量
     */
    UFUNCTION(BlueprintCallable, Category = "GameAssetCore|Pin")
    static int32 PinAssets(const TArray<TSoftObjectPtr<UObject>>& Assets);

    /**
     * @brief 批量解除一组软引用资产的内存常驻锁定
     * @param Assets 目标软引用资产数组
     * @return 成功递减或解除锁定的资产数量
     */
    UFUNCTION(BlueprintCallable, Category = "GameAssetCore|Pin")
    static int32 UnpinAssets(const TArray<TSoftObjectPtr<UObject>>& Assets);

    /**
     * @brief 获取指定资产当前的常驻保活引用计数
     * @param Asset 目标软引用资产
     * @return 引用计数值，未锁定返回 0
     */
    UFUNCTION(BlueprintPure, Category = "GameAssetCore|Pin")
    static int32 GetAssetPinRefCount(const TSoftObjectPtr<UObject>& Asset);

    /**
     * @brief 获取指定已常驻保活资产的底层实例指针
     * @param Asset 目标软引用资产
     * @return 常驻资产实例指针，未锁定返回 nullptr
     */
    UFUNCTION(BlueprintPure, Category = "GameAssetCore|Pin")
    static UObject* GetPinnedAsset(const TSoftObjectPtr<UObject>& Asset);

    /**
     * @brief 检查指定软引用资产当前是否已驻留内存并可零阻塞同步访问
     * @param Asset 目标软引用资产
     * @return 已加载驻留且有效返回 true
     */
    UFUNCTION(BlueprintPure, Category = "GameAssetCore|Query")
    static bool IsAssetLoaded(const TSoftObjectPtr<UObject>& Asset);

    /**
     * @brief 获取已驻留内存的软引用资产实例指针（未加载时直接返回 nullptr，绝不触发同步磁盘加载）
     * @param Asset 目标软引用资产
     * @return 已驻留有效资产实例指针
     */
    UFUNCTION(BlueprintPure, Category = "GameAssetCore|Query")
    static UObject* GetLoadedAsset(const TSoftObjectPtr<UObject>& Asset);

    /**
     * @brief 获取全局当前已常驻保活的资产种类总数
     * @return 常驻保活资产种类数量
     */
    UFUNCTION(BlueprintPure, Category = "GameAssetCore|Pin")
    static int32 GetTotalPinnedAssetCount();

    /**
     * @brief 导出当前所有处于常驻保活状态的资产路径清单
     * @return 常驻保活资产路径数组
     */
    UFUNCTION(BlueprintPure, Category = "GameAssetCore|Pin")
    static TArray<FSoftObjectPath> GetAllPinnedAssetPaths();
};
