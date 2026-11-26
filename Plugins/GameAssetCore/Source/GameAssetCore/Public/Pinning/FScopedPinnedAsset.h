/**
 * @file FScopedPinnedAsset.h
 * @brief 遵循 RAII 原则的资产内存保活作用域守卫（单次加锁构造、O(1) 缓存访问、离开作用域自动解除锁定）
 */

#pragma once

#include "CoreMinimal.h"
#include "Templates/Casts.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"

/**
 * @brief 遵循 RAII 原则的资产内存保活作用域守卫
 */
class GAMEASSETCORE_API FScopedPinnedAsset {
public:
    FScopedPinnedAsset() = default;

    /**
     * @brief 构造并立即尝试常驻锁定目标资产路径
     * @param InPath 目标资产软路径
     */
    explicit FScopedPinnedAsset(FSoftObjectPath InPath);

    /**
     * @brief 由强类型软对象引用构造并尝试常驻保活
     * @tparam T 目标资产类型
     * @param InSoftObject 软对象引用
     */
    template <typename T>
    explicit FScopedPinnedAsset(const TSoftObjectPtr<T>& InSoftObject)
        : FScopedPinnedAsset(InSoftObject.ToSoftObjectPath()) {
    }

    /**
     * @brief 由强类型软类引用构造并尝试常驻保活
     * @tparam T 目标类基类类型
     * @param InSoftClass 软类引用
     */
    template <typename T>
    explicit FScopedPinnedAsset(const TSoftClassPtr<T>& InSoftClass)
        : FScopedPinnedAsset(InSoftClass.ToSoftObjectPath()) {
    }

    /**
     * @brief 析构函数：若当前持有有效锁定则自动递减引用计数
     */
    ~FScopedPinnedAsset();

    FScopedPinnedAsset(const FScopedPinnedAsset&) = delete;
    FScopedPinnedAsset& operator=(const FScopedPinnedAsset&) = delete;

    /**
     * @brief 移动构造函数（无锁转移保活所有权并清空源守卫）
     * @param Other 源保活守卫
     */
    FScopedPinnedAsset(FScopedPinnedAsset&& Other) noexcept;

    /**
     * @brief 移动赋值运算符（释放当前锁定并无锁接管源守卫所有权）
     * @param Other 源保活守卫
     * @return 自身引用
     */
    FScopedPinnedAsset& operator=(FScopedPinnedAsset&& Other) noexcept;

    /**
     * @brief 主动提前解除当前持有的保活锁定
     */
    void Release() noexcept;

    /**
     * @brief 查询当前守卫是否持有有效的常驻保活锁定
     * @return 持有有效锁定返回 true
     */
    [[nodiscard]] bool IsPinned() const noexcept;

    /**
     * @brief 获取当前保活的资产软路径（未锁定时为空路径）
     * @return 资产软路径常量引用
     */
    [[nodiscard]] const FSoftObjectPath& GetPath() const noexcept;

    /**
     * @brief O(1) 获取栈上缓存的已保活底层 UObject 实例指针
     * @return 已保活资产实例指针，未锁定返回 nullptr
     */
    [[nodiscard]] UObject* Get() const noexcept;

    /**
     * @brief 获取强类型的已保活资产实例指针
     * @tparam T 目标 UObject 派生资产类型
     * @return 强类型资产指针，未锁定或类型不兼容返回 nullptr
     */
    template <typename T>
    [[nodiscard]] T* Get() const noexcept {
        return Cast<T>(Get());
    }

    /**
     * @brief 布尔显式类型转换操作符
     * @return 持有有效锁定返回 true
     */
    [[nodiscard]] explicit operator bool() const noexcept {
        return IsPinned();
    }

private:
    FSoftObjectPath PinnedPath;
    UObject* CachedAsset = nullptr;
};
