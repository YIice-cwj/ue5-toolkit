/**
 * @file FStaticDataCache.cpp
 * @brief 静态数据快照缓存容器类实现
 */
#include "Generated/Data/StaticData/FStaticDataCache.h"

void FStaticDataCache::Reset() {
    ItemCompositesCache.Reset();
    ShopCompositesCache.Reset();
}

FVersionedCache<FItemCompositeData>& FStaticDataCache::ItemComposites() const {
    return ItemCompositesCache;
}

FVersionedCache<FShopCompositeData>& FStaticDataCache::ShopComposites() const {
    return ShopCompositesCache;
}
