// Automatically generated static data snapshot carrier. Do not edit manually.
#pragma once

#include "CoreMinimal.h"

#include "Generated/Data/StaticData/FStaticDataCache.h"
#include "Generated/Data/StaticData/StaticDataTypes.h"

/**
 * @brief 静态数据快照（发布后只读；禁止拷贝与移动）
 */
class GAMEDATASAMPLES_API FStaticDataSnapshot {
public:
    /** 数据版本号（与快照同代绑定，自 1 起递增） */
    uint32 DataVersion = 0;

    /** 各域复合聚合缓存（行 join 后的强类型视图） */
    FStaticDataCache Cache;

    /** Item 域行表 */
    FItemStaticData ItemData;

    /** Shop 域行表 */
    FShopStaticData ShopData;

    FStaticDataSnapshot() = default;
    ~FStaticDataSnapshot() = default;
    FStaticDataSnapshot(const FStaticDataSnapshot&) = delete;
    FStaticDataSnapshot& operator=(const FStaticDataSnapshot&) = delete;
    FStaticDataSnapshot(FStaticDataSnapshot&&) = delete;
    FStaticDataSnapshot& operator=(FStaticDataSnapshot&&) = delete;
};
