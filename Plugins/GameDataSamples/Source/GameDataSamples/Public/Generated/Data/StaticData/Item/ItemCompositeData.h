/**
 * @file ItemCompositeData.h
 * @brief 复合数据容器
 */
#pragma once

#include "CoreMinimal.h"

#include "Generated/DataTables/Item/ItemDataTableRowTypes.h"
#include "ItemCompositeData.generated.h"

/**
 * @brief Item 单体聚合数据（汇集该模块所有分表的单行数据）
 */
USTRUCT(BlueprintType)
struct GAMEDATASAMPLES_API FItemCompositeData {
    GENERATED_BODY()

public:
    /** Item数据行 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Composite", meta = (DisplayName = "Item数据行"))
    FItemRow Item;

};
