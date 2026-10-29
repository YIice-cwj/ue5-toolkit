/**
 * @file ShopGoodsRow.h
 * @brief 数据表行数据结构（生成自 DT_ShopGoods.xlsx）
 */
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ShopGoodsRow.generated.h"

/**
 * @brief ShopGoods数据行
 */
USTRUCT(BlueprintType)
struct FShopGoodsRow : public FTableRowBase {
    GENERATED_BODY()

public:
    /** 商品名称 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShopGoods", meta = (DisplayName = "商品名称"))
    FString GoodsName;

    /** 关联物品(显式外键) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShopGoods", meta = (DisplayName = "关联物品(显式外键)"))
    FName ItemId;

    /** 展示物品(前缀推导) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShopGoods", meta = (DisplayName = "展示物品(前缀推导)"))
    FName DisplayItemId;

    /** 售价 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShopGoods", meta = (DisplayName = "售价"))
    int32 Price = 0;

};
