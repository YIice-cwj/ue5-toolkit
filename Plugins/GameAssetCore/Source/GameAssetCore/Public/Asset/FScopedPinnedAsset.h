/**
 * @file FScopedPinnedAsset.h
 * @brief 遵循 RAII 原则的资产内存保活作用域守卫（离开作用域自动解除锁定）
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"

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
     * @brief 由软引用模板对象构造并尝试保活
     * @tparam T 资产类型
     * @param InSoftObject 软引用对象
     */
    template <typename T>
    explicit FScopedPinnedAsset(const TSoftObjectPtr<T>& InSoftObject)
        : FScopedPinnedAsset(InSoftObject.ToSoftObjectPath()) {
    }

    /**
     * @brief 由软类引用模板对象构造并尝试保活
     * @tparam T 类类型
     * @param InSoftClass 软类引用对象
     */
    template <typename T>
    explicit FScopedPinnedAsset(const TSoftClassPtr<T>& InSoftClass)
        : FScopedPinnedAsset(InSoftClass.ToSoftObjectPath()) {
    }

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

    /**
     * @brief 获取当前保活的底层原生 UObject 实例指针
     * @return 资产实例指针，未锁定或已被回收返回 nullptr
     */
    [[nodiscard]] UObject* Get() const noexcept;

    /**
     * @brief 获取强类型的保活资产实例指针
     * @tparam T 目标资产类型
     * @return 强类型资产指针，未锁定或类型不匹配返回 nullptr
     */
    template <typename T>
    [[nodiscard]] T* Get() const noexcept {
        return Cast<T>(Get());
    }

    /**
     * @brief 布尔类型转换操作符
     * @return 持有有效锁定返回 true
     */
    explicit operator bool() const noexcept {
        return IsPinned();
    }

private:
    FSoftObjectPath PinnedPath;
    UObject* CachedAsset = nullptr;
};
