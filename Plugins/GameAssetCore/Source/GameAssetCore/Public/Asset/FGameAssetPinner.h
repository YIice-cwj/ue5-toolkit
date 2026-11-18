/**
 * @file FGameAssetPinner.h
 * @brief 资产内存常驻锁定与引用计数管理器
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "Types/TGameResult.h"

/**
 * @brief 资产内存常驻保活与引用计数纯静态管理器（线程安全）
 */
class GAMEASSETCORE_API FGameAssetPinner {
public:
    FGameAssetPinner() = delete;

    /**
     * @brief 锁定资产路径在内存中的常驻状态（内部增加引用计数，防止未挂载被 GC 误杀回收）
     * @param AssetPath 待锁定的资产软路径
     * @return 锁定成功返回成功态，路径无效或同步加载失败返回对应 FGameError
     */
    static TGameStatusResult<void> PinAsset(const FSoftObjectPath& AssetPath);

    /**
     * @brief 解除资产路径的内存常驻锁定（内部递减引用计数，计数归零时释放句柄）
     * @param AssetPath 待解除锁定的资产软路径
     * @return 扣减或解除成功返回成功态，路径无效或未被锁定返回对应 FGameError
     */
    static TGameStatusResult<void> UnpinAsset(const FSoftObjectPath& AssetPath);

    /**
     * @brief 检查指定资产路径当前是否处于保活锁定状态
     * @param AssetPath 待查询的资产软路径
     * @return 处于保活状态返回 true
     */
    [[nodiscard]] static bool IsAssetPinned(const FSoftObjectPath& AssetPath);

    /**
     * @brief 获取指定资产路径的当前锁定引用计数
     * @param AssetPath 待查询的资产软路径
     * @return 引用计数数值，未锁定返回 0
     */
    [[nodiscard]] static int32 GetPinRefCount(const FSoftObjectPath& AssetPath);

    /**
     * @brief 获取当前处于常驻保活状态的底层资产实例指针
     * @param AssetPath 目标资产软路径
     * @return 资产实例指针，未锁定或已被回收返回 nullptr
     */
    [[nodiscard]] static UObject* GetPinnedAsset(const FSoftObjectPath& AssetPath);

    /**
     * @brief 获取当前处于常驻保活状态的强类型资产实例指针
     * @tparam T 目标资产类型
     * @param AssetPath 目标资产软路径
     * @return 强类型资产指针，未锁定或类型不匹配返回 nullptr
     */
    template <typename T>
    [[nodiscard]] static T* GetPinnedAsset(const FSoftObjectPath& AssetPath) {
        return Cast<T>(GetPinnedAsset(AssetPath));
    }

    /**
     * @brief 获取当前全局已处于锁定常驻状态的资产总数
     * @return 锁定资产种类总数
     */
    [[nodiscard]] static int32 GetTotalPinnedCount();

    /**
     * @brief 导出当前所有处于常驻锁定状态的资产路径清单
     * @param OutPaths 导出的资产路径数组（输出参数）
     */
    static void GetAllPinnedPaths(TArray<FSoftObjectPath>& OutPaths);

    /**
     * @brief 清空全部资产的保活锁定引用（主要用于测试复位或关卡全局重置）
     */
    static void ClearAllPinnedAssets();
};
