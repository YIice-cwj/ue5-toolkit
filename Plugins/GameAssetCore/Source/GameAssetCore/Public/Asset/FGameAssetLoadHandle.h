/**
 * @file FGameAssetLoadHandle.h
 * @brief 异步资产流式加载控制句柄
 */

#pragma once

#include "CoreMinimal.h"

struct FStreamableHandle;

/**
 * @brief 封装引擎 FStreamableHandle 的强类型加载操作句柄
 */
class GAMEASSETCORE_API FGameAssetLoadHandle {
public:
    FGameAssetLoadHandle() = default;

    /**
     * @brief 由底层流式加载句柄构造包装句柄
     * @param InHandle 引擎流式加载句柄共享指针
     */
    explicit FGameAssetLoadHandle(TSharedPtr<FStreamableHandle> InHandle) noexcept;

    /**
     * @brief 取消正在进行中的异步加载并释放底层流式句柄引用
     */
    void Cancel();

    /**
     * @brief 释放句柄对底层资产的流式保活引用（不触发取消回调，允许后续正常 GC）
     */
    void Reset();

    /**
     * @brief 检查当前句柄是否关联有效的底层流式加载请求且未被取消
     * @return 关联有效请求返回 true
     */
    [[nodiscard]] bool IsValid() const noexcept;

    /**
     * @brief 布尔类型转换操作符
     * @return 关联有效请求且未被取消返回 true
     */
    explicit operator bool() const noexcept {
        return IsValid();
    }

    /**
     * @brief 检查关联的异步加载请求是否仍在后台执行中
     * @return 正在加载返回 true
     */
    [[nodiscard]] bool IsLoading() const;

    /**
     * @brief 检查关联的异步加载请求是否已加载完成
     * @return 已完成返回 true
     */
    [[nodiscard]] bool HasCompleted() const;

    /**
     * @brief 检查关联的异步加载请求是否发生错误或失败
     * @return 加载出错返回 true
     */
    [[nodiscard]] bool HasError() const;

    /**
     * @brief 检查关联的异步加载请求是否已被取消
     * @return 已取消返回 true
     */
    [[nodiscard]] bool WasCanceled() const;

    /**
     * @brief 获取当前加载进度比例
     * @return 进度比例 [0.0f, 1.0f]（空句柄返回 0.0f，已完成返回 1.0f）
     */
    [[nodiscard]] float GetProgress() const;

    /**
     * @brief 获取异步加载完成后的底层资产实例指针
     * @return 资产实例指针，未完成、已取消或加载失败返回 nullptr
     */
    [[nodiscard]] UObject* GetLoadedAsset() const;

    /**
     * @brief 获取异步加载完成后的强类型资产实例指针
     * @tparam T 目标资产类型
     * @return 强类型资产指针，未完成或类型不匹配返回 nullptr
     */
    template <typename T>
    [[nodiscard]] T* GetLoadedAsset() const {
        return Cast<T>(GetLoadedAsset());
    }

    /**
     * @brief 获取批量异步加载完成后的全部资产实例数组
     * @param OutLoadedAssets 输出资产指针数组
     */
    void GetLoadedAssets(TArray<UObject*>& OutLoadedAssets) const;

    /**
     * @brief 获取批量异步加载完成后的强类型资产实例数组
     * @tparam T 目标资产类型
     * @param OutLoadedAssets 输出强类型资产指针数组（自动过滤类型不匹配项）
     */
    template <typename T>
    void GetLoadedAssets(TArray<T*>& OutLoadedAssets) const {
        OutLoadedAssets.Reset();
        TArray<UObject*> RawAssets;
        GetLoadedAssets(RawAssets);
        OutLoadedAssets.Reserve(RawAssets.Num());
        for (UObject* Raw : RawAssets) {
            if (T* Typed = Cast<T>(Raw)) {
                OutLoadedAssets.Add(Typed);
            }
        }
    }

private:
    TSharedPtr<FStreamableHandle> StreamableHandle;
};
