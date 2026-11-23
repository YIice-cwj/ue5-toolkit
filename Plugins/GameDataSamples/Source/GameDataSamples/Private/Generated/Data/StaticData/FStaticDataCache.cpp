/**
 * @file FStaticDataCache.cpp
 * @brief 静态数据快照缓存容器类实现
 */
#include "Generated/Data/StaticData/FStaticDataCache.h"

/**
 * @brief 获取 Item 域聚合缓存
 * @return 缓存引用
 */
FVersionedCache<FItemCompositeData>& FStaticDataCache::ItemComposites() const {
    return ItemCompositesCache;
}

/**
 * @brief 获取 Shop 域聚合缓存
 * @return 缓存引用
 */
FVersionedCache<FShopCompositeData>& FStaticDataCache::ShopComposites() const {
    return ShopCompositesCache;
}
