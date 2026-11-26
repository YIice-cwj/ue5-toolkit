/**
 * @file FGameAssetLoadHandle.h
 * @brief 异步资产流式加载强类型控制句柄声明
 */

#pragma once

#include "CoreMinimal.h"
#include "Templates/Casts.h"
#include "Templates/Function.h"
#include "UObject/Object.h"

struct FStreamableHandle;

/**
 * @brief 封装引擎 FStreamableHandle 的强类型加载操作与观测句柄
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
     * @brief 取消正在进行或已激活的流式加载（立即阻止完成回调并释放底层资产引用，且保持 WasCanceled 状态可查）
     */
    void Cancel();

    /**
     * @brief 释放句柄对底层资产的流式保活引用并清空句柄（不触发取消语义，允许后续正常 GC）
     */
    void Reset();

    /**
     * @brief 检查当前句柄是否关联活跃有效（未取消且未释放）的底层流式加载请求
     * @return 关联活跃请求返回 true
     */
    [[nodiscard]] bool IsValid() const noexcept;

    /**
     * @brief 布尔显式类型转换操作符
     * @return 关联活跃请求返回 true
     */
    [[nodiscard]] explicit operator bool() const noexcept {
        return IsValid();
    }

    /**
     * @brief 检查关联的异步加载请求是否仍在后台加载中
     * @return 正在加载且未取消返回 true
     */
    [[nodiscard]] bool IsLoading() const;

    /**
     * @brief 检查关联的异步加载请求是否已正常加载完成（未取消）
     * @return 已正常完成返回 true
     */
    [[nodiscard]] bool HasCompleted() const;

    /**
     * @brief 检查关联的异步加载请求是否发生底层错误
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
     * @return 进度比例 [0.0f, 1.0f]（空句柄、已释放或已取消返回 0.0f，已完成返回 1.0f）
     */
    [[nodiscard]] float GetProgress() const;

    /**
     * @brief 获取异步加载完成后的首个有效底层资产实例指针
     * @return 资产实例指针，未完成、已取消、已释放或加载失败返回 nullptr
     */
    [[nodiscard]] UObject* GetLoadedAsset() const;

    /**
     * @brief 获取异步加载完成后的强类型资产实例指针
     * @tparam T 目标 UObject 派生资产类型
     * @return 强类型资产指针，未完成或类型不兼容返回 nullptr
     */
    template <typename T>
    [[nodiscard]] T* GetLoadedAsset() const {
        return Cast<T>(GetLoadedAsset());
    }

    /**
     * @brief 零额外数组分配就地遍历全部已成功加载的有效资产实例
     * @param Visitor 接收每个有效 UObject 指针的访问闭包
     */
    void ForEachLoadedAsset(TFunctionRef<void(UObject*)> Visitor) const;

    /**
     * @brief 获取批量异步加载完成后的全部有效资产实例数组
     * @param OutLoadedAssets 输出资产指针数组
     */
    void GetLoadedAssets(TArray<UObject*>& OutLoadedAssets) const;

    /**
     * @brief 获取批量异步加载完成后的强类型资产实例数组（单遍遍历零中间数组分配，自动过滤不兼容项）
     * @tparam T 目标 UObject 派生资产类型
     * @param OutLoadedAssets 输出强类型资产指针数组
     */
    template <typename T>
    void GetLoadedAssets(TArray<T*>& OutLoadedAssets) const {
        OutLoadedAssets.Reset();
        ForEachLoadedAsset([&OutLoadedAssets](UObject* LoadedObject) {
            if (T* Typed = Cast<T>(LoadedObject)) {
                OutLoadedAssets.Add(Typed);
            }
        });
    }

private:
    TSharedPtr<FStreamableHandle> StreamableHandle;
};
