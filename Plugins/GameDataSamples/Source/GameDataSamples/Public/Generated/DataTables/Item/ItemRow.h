// Automatically generated from DT_Item.xlsx. Do not edit manually.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "ItemRow.generated.h"

/**
 * @brief Item数据行
 */
USTRUCT(BlueprintType)
struct FItemRow : public FTableRowBase {
    GENERATED_BODY()

public:
    /** 物品名称 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (DisplayName = "物品名称"))
    FString ItemName;

    /** 基础攻击力 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (DisplayName = "基础攻击力"))
    int32 BaseAttack = 0;

    /** 金币价值 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (DisplayName = "金币价值"))
    float GoldCost = 0.0f;

    /** 是否可用 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (DisplayName = "是否可用"))
    bool bIsUsable = false;

    /** 玩法标签 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (DisplayName = "玩法标签"))
    FGameplayTag ItemTag;

};
