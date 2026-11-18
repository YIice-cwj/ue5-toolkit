/**
 * @file UGameAssetBlueprintLibrary.h
 * @brief 资产常驻保活与状态查询蓝图函数库（向蓝图与策划系统开放资产常驻与快速查询能力）
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/SoftObjectPtr.h"

#include "UGameAssetBlueprintLibrary.generated.h"

/**
 * @brief 面向蓝图的 GameAssetCore 资产常驻保活与查询函数库
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
     * @brief 锁定指定软引用资产常驻内存（防 GC 误杀回收，增加引用计数）
     * @param Asset 目标软引用资产
     * @return 锁定或增计数成功返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameAssetCore|Pin")
    static bool PinAsset(const TSoftObjectPtr<UObject>& Asset);

    /**
     * @brief 解除指定软引用资产的内存常驻锁定（扣减引用计数，归零时释放）
     * @param Asset 目标软引用资产
     * @return 解锁或减计数成功返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameAssetCore|Pin")
    static bool UnpinAsset(const TSoftObjectPtr<UObject>& Asset);

    /**
     * @brief 批量锁定一组软引用资产常驻内存
     * @param Assets 目标软引用资产数组
     * @return 成功增加保活锁定的资产数量
     */
    UFUNCTION(BlueprintCallable, Category = "GameAssetCore|Pin")
    static int32 PinAssets(const TArray<TSoftObjectPtr<UObject>>& Assets);

    /**
     * @brief 批量解除一组软引用资产的内存常驻锁定
     * @param Assets 目标软引用资产数组
     * @return 成功扣减或解除锁定的资产数量
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
     * @brief 获取指定已保活资产的底层实例指针
     * @param Asset 目标软引用资产
     * @return 实例指针，未锁定或未加载返回 nullptr
     */
    UFUNCTION(BlueprintPure, Category = "GameAssetCore|Pin")
    static UObject* GetPinnedAsset(const TSoftObjectPtr<UObject>& Asset);

    /**
     * @brief 检查指定软引用资产当前是否已驻留内存并可同步访问
     * @param Asset 目标软引用资产
     * @return 已加载驻留返回 true
     */
    UFUNCTION(BlueprintPure, Category = "GameAssetCore|Query")
    static bool IsAssetLoaded(const TSoftObjectPtr<UObject>& Asset);

    /**
     * @brief 获取已驻留内存的软引用资产裸指针（若未加载则直接返回 nullptr，不触发阻塞加载）
     * @param Asset 目标软引用资产
     * @return 资产实例指针
     */
    UFUNCTION(BlueprintPure, Category = "GameAssetCore|Query")
    static UObject* GetLoadedAsset(const TSoftObjectPtr<UObject>& Asset);

    /**
     * @brief 获取全局当前已常驻保活的资产种类总数
     * @return 保活资产总数
     */
    UFUNCTION(BlueprintPure, Category = "GameAssetCore|Pin")
    static int32 GetTotalPinnedAssetCount();

    /**
     * @brief 导出当前所有处于常驻保活状态的资产路径清单
     * @return 资产路径数组
     */
    UFUNCTION(BlueprintPure, Category = "GameAssetCore|Pin")
    static TArray<FSoftObjectPath> GetAllPinnedAssetPaths();
};
