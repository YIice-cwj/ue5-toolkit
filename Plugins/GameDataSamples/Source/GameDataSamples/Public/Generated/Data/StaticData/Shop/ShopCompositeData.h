/**
 * @file ShopCompositeData.h
 * @brief 复合数据容器
 */
#pragma once

#include "CoreMinimal.h"

#include "Generated/DataTables/Shop/ShopDataTableRowTypes.h"
#include "ShopCompositeData.generated.h"

/**
 * @brief Shop 单体聚合数据（汇集该模块所有分表的单行数据）
 */
USTRUCT(BlueprintType)
struct GAMEDATASAMPLES_API FShopCompositeData {
    GENERATED_BODY()

public:
    /** ShopGoods数据行 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop|Composite", meta = (DisplayName = "ShopGoods数据行"))
    FShopGoodsRow Goods;

};
