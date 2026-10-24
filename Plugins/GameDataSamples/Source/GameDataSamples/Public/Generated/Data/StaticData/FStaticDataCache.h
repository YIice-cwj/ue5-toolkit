// Automatically generated static data cache holder. Do not edit manually.
#pragma once

#include "CoreMinimal.h"

#include "Generated/Data/StaticData/StaticDataTypes.h"
#include "Cache/FVersionedCache.h"

/**
 * @brief 各域聚合缓存容器（挂接于 UStaticDataSubsystem，随其生命周期）
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
     * @brief 清空全部域缓存（热更提交后调用，旧指针契约失效）
     */
    void Reset();

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
    /** 各域逻辑只读缓存（mutable：const 快照上允许查找与缓存内部惰性填充） */
    mutable FVersionedCache<FItemCompositeData> ItemCompositesCache{FName(TEXT("Item"))};
    mutable FVersionedCache<FShopCompositeData> ShopCompositesCache{FName(TEXT("Shop"))};
};
