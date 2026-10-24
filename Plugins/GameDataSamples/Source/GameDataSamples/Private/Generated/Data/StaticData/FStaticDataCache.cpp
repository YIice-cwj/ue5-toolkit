// Automatically generated static data cache holder implementation. Do not edit manually.
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
