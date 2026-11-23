/**
 * @file FStaticDataCache.h
 * @brief 静态数据快照缓存容器类
 */
#pragma once

#include "CoreMinimal.h"

#include "Cache/FVersionedCache.h"
#include "Generated/Data/StaticData/StaticDataTypes.h"

/**
 * @brief 各域聚合缓存容器（挂接于 FStaticDataSnapshot，随不可变快照生命周期）
 */
class GAMEDATASAMPLES_API FStaticDataCache {
public:
    FStaticDataCache() = default;
    ~FStaticDataCache() = default;
    FStaticDataCache(const FStaticDataCache&) = delete;
    FStaticDataCache& operator=(const FStaticDataCache&) = delete;
    FStaticDataCache(FStaticDataCache&&) = delete;
    FStaticDataCache& operator=(FStaticDataCache&&) = delete;

    /**
     * @brief 获取 Item 域聚合缓存
     * @return 缓存引用（逻辑只读，const 快照上同样可查找）
     */
    [[nodiscard]] FVersionedCache<FItemCompositeData>& ItemComposites() const;

    /**
     * @brief 获取 Shop 域聚合缓存
     * @return 缓存引用（逻辑只读，const 快照上同样可查找）
     */
    [[nodiscard]] FVersionedCache<FShopCompositeData>& ShopComposites() const;

private:
    /**
     * @brief Item 域逻辑只读聚合缓存（mutable：const 快照上允许查找与惰性填充）
     */
    mutable FVersionedCache<FItemCompositeData> ItemCompositesCache{FName(TEXT("Item"))};
    /**
     * @brief Shop 域逻辑只读聚合缓存（mutable：const 快照上允许查找与惰性填充）
     */
    mutable FVersionedCache<FShopCompositeData> ShopCompositesCache{FName(TEXT("Shop"))};
};
