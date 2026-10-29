/**
 * @file ShopStaticDataInterface.h
 * @brief 商店静态数据业务门面接口
 */

#pragma once

#include "CoreMinimal.h"

#include "Generated/DataTables/Shop/ShopGoodsRow.h"
#include "Generated/Data/StaticData/Shop/ShopStaticData.h"
#include "Generated/Data/StaticData/FStaticDataCache.h"
#include "Generated/Data/StaticData/FStaticDataSnapshot.h"

/**
 * @brief Shop 静态数据业务接口（门面：编排查询与服务）
 * @note 此文件首次由生成脚本创建，后续重新生成数据表时【绝不会被覆盖】。
 *       开发者可在此自由扩展该模块专属的高级业务查询与数据组装接口。
 */
class GAMEDATASAMPLES_API FShopStaticDataInterface {
public:
    FShopStaticDataInterface() = delete;

    /**
     * @brief 获取单体聚合数据（FName 键，快照持有语义，未命中返回空）
     * @param InSnapshot 静态数据快照（由 UStaticDataSubsystem::GetSnapshot 取得）
     * @param InKey 唯一索引键
     * @return 聚合数据快照（引用计数兜底，可跨版本持有），未命中或快照为空返回空
     */
    static TSharedPtr<const FShopCompositeData> FindCompositeData(const TSharedPtr<const FStaticDataSnapshot>& InSnapshot, const FName& InKey);

    /**
     * @brief 预热聚合缓存（加载/热更提交后调用，按主表全量键预热）
     * @param InCache 聚合缓存
     * @param InData 域静态数据容器
     */
    static void WarmUpCompositeCache(FStaticDataCache& InCache, const FShopStaticData& InData);
};
