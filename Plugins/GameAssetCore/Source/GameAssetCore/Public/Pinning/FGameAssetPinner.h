/**
 * @file FGameAssetPinner.h
 * @brief 资产内存常驻锁定、FGCObject 强引用保活与引用计数管理器声明
 */

#pragma once

#include "CoreMinimal.h"
#include "Templates/Casts.h"
#include "Types/TGameResult.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"

/**
 * @brief 资产内存常驻保活与引用计数静态管理器
 * @note 已驻留内存资产的常驻锁定、引用计数增减与状态查询均支持多线程并发调用；对尚未驻留内存的磁盘资产首次同步加载须在 GameThread 调用。
 */
class GAMEASSETCORE_API FGameAssetPinner {
public:
    FGameAssetPinner() = delete;

    /**
     * @brief 初始化全局常驻保活管理器的 GC 强引用收集器（由模块启动自动调用）
     */
    static void Startup();

    /**
     * @brief 关闭全局常驻保活管理器，释放全部常驻资产并注销 GC 引用器（由模块卸载自动调用）
     */
    static void Shutdown();

    /**
     * @brief 锁定指定资产路径常驻内存并递增引用计数（经 FGCObject 强引用保护防止被引擎 GC 回收）
     * @param AssetPath 待锁定的目标资产软路径
     * @return 成功返回已常驻保活的资产实例指针，路径非法、非主线程冷加载或磁盘加载失败返回对应 FGameError
     */
    static TGameStatusResult<UObject*> PinAsset(const FSoftObjectPath& AssetPath);

    /**
     * @brief 递减指定资产路径的常驻引用计数（计数归零时自动移除强引用与流式句柄）
     * @param AssetPath 待解除锁定的目标资产软路径
     * @return 扣减或释放成功返回成功态，路径非法或当前未被常驻锁定返回对应 FGameError
     */
    static TGameStatusResult<void> UnpinAsset(const FSoftObjectPath& AssetPath);

    /**
     * @brief 检查指定资产路径当前是否处于有效常驻保活状态
     * @param AssetPath 待查询的资产软路径
     * @return 处于常驻保活状态返回 true
     */
    [[nodiscard]] static bool IsAssetPinned(const FSoftObjectPath& AssetPath);

    /**
     * @brief 获取指定资产路径当前的常驻锁定引用计数
     * @param AssetPath 待查询的资产软路径
     * @return 当前正整数引用计数值，未锁定返回 0
     */
    [[nodiscard]] static int32 GetPinRefCount(const FSoftObjectPath& AssetPath);

    /**
     * @brief O(1) 读取当前处于常驻保活状态的资产实例指针
     * @param AssetPath 目标资产软路径
     * @return 常驻资产实例指针，未锁定返回 nullptr
     */
    [[nodiscard]] static UObject* GetPinnedAsset(const FSoftObjectPath& AssetPath);

    /**
     * @brief O(1) 读取当前处于常驻保活状态的强类型资产实例指针
     * @tparam T 目标 UObject 派生资产类型
     * @param AssetPath 目标资产软路径
     * @return 强类型资产指针，未锁定或类型不兼容返回 nullptr
     */
    template <typename T>
    [[nodiscard]] static T* GetPinnedAsset(const FSoftObjectPath& AssetPath) {
        return Cast<T>(GetPinnedAsset(AssetPath));
    }

    /**
     * @brief 获取当前全局处于常驻保活状态的资产种类总数
     * @return 常驻资产条目数量
     */
    [[nodiscard]] static int32 GetTotalPinnedCount();

    /**
     * @brief 导出当前所有处于常驻保活状态的资产路径快照
     * @param OutPaths 接收常驻资产软路径的输出数组
     */
    static void GetAllPinnedPaths(TArray<FSoftObjectPath>& OutPaths);

    /**
     * @brief 清空全部资产的常驻保活引用并释放底层流式句柄
     */
    static void ClearAllPinnedAssets();
};
