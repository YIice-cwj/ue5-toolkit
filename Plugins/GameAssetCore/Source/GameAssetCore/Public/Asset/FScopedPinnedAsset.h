/**
 * @file FScopedPinnedAsset.h
 * @brief 遵循 RAII 原则的资产内存保活作用域守卫（离开作用域自动解除锁定）
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

/**
 * @brief 遵循 RAII 原则的资产内存保活作用域守卫（离开作用域自动解除保活）
 */
class GAMEASSETCORE_API FScopedPinnedAsset {
public:
    FScopedPinnedAsset() = default;

    /**
     * @brief 构造并立即尝试锁定目标资产路径
     * @param InPath 目标资产路径
     */
    explicit FScopedPinnedAsset(FSoftObjectPath InPath);

    /**
     * @brief 析构时若持有锁定则自动解除保活
     */
    ~FScopedPinnedAsset();

    FScopedPinnedAsset(const FScopedPinnedAsset&) = delete;
    FScopedPinnedAsset& operator=(const FScopedPinnedAsset&) = delete;

    /**
     * @brief 移动构造函数（接管保活路径并清空源对象）
     * @param Other 源保活守卫
     */
    FScopedPinnedAsset(FScopedPinnedAsset&& Other) noexcept;

    /**
     * @brief 移动赋值运算符（释放当前锁定并接管源对象锁定）
     * @param Other 源保活守卫
     * @return 自身引用
     */
    FScopedPinnedAsset& operator=(FScopedPinnedAsset&& Other) noexcept;

    /**
     * @brief 主动提前解除保活锁定
     */
    void Release() noexcept;

    /**
     * @brief 查询当前守卫是否持有有效的保活锁定
     * @return 持有锁定返回 true
     */
    [[nodiscard]] bool IsPinned() const noexcept;

    /**
     * @brief 获取当前保活的资产软路径
     * @return 资产软路径引用
     */
    [[nodiscard]] const FSoftObjectPath& GetPath() const noexcept;

private:
    FSoftObjectPath PinnedPath;
};
