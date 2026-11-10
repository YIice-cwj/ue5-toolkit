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
     * @brief 检查当前句柄是否关联有效的底层流式加载请求且未被取消
     * @return 关联有效请求返回 true
     */
    [[nodiscard]] bool IsValid() const noexcept;

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
     * @brief 获取当前加载进度比例
     * @return 进度比例 [0.0f, 1.0f]（空句柄返回 0.0f，已完成返回 1.0f）
     */
    [[nodiscard]] float GetProgress() const;

private:
    TSharedPtr<FStreamableHandle> StreamableHandle;
};
