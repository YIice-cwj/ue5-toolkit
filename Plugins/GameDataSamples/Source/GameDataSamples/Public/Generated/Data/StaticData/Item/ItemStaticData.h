// Automatically generated static data container for Item. Do not edit manually.
#pragma once

#include "CoreMinimal.h"

#include "Misc/Paths.h"
#include "Utils/DataTableLoaderUtils.h"
#include "Data/StaticData/FStaticDataRepository.h"
#include "Generated/DataTables/Item/ItemDataTableRowTypes.h"
#include "ItemCompositeData.h"
#include "ItemStaticData.generated.h"

/**
 * @brief Item 静态数据容器（存储该模块所有分表的 Map，由 UStaticDataSubsystem 持有）
 */
USTRUCT(BlueprintType)
struct GAMEDATASAMPLES_API FItemStaticData {
    GENERATED_BODY()

public:
    /**
     * @brief 从指定 JSON 目录加载当前模块所有分表数据
     * @param InJsonDir JSON 所在根目录，缺省自动使用 [项目目录]/DataTables_Json/Item
     * @return 是否所有分表均加载成功
     */
    bool LoadAllFromJson(const FString& InJsonDir = TEXT("")) {
        const FString BaseDir = InJsonDir.IsEmpty()
            ? (FPaths::ProjectDir() / TEXT("DataTables_Json") / TEXT("Item"))
            : InJsonDir;

        bool bAllSuccess = true;
        bAllSuccess &= FDataTableLoaderUtils::LoadTableFromJson(BaseDir / TEXT("DT_Item.json"), ItemMap);
        return bAllSuccess;
    }

    /**
     * @brief 从生成式二进制仓库整体装载当前模块所有分表数据
     * @param Repository 已登记读写器并完成 .bin 装载的仓库
     * @return 是否所有分表均提取成功（失败时可用仓库错误码定位原因）
     */
    bool LoadAllFromRepository(const FStaticDataRepository& Repository) {
        bool bAllSuccess = true;
        bAllSuccess &= Repository.ExtractTable(ItemMap);
        return bAllSuccess;
    }

    /**
     * @brief 将当前模块所有分表数据写入生成式二进制仓库
     * @param Repository 目标仓库（须已登记读写器）
     * @return 是否所有分表均写入成功（失败时可用仓库错误码定位原因）
     */
    bool AddAllToRepository(FStaticDataRepository& Repository) const {
        bool bAllSuccess = true;
        bAllSuccess &= Repository.AddTable(ItemMap);
        return bAllSuccess;
    }

    /**
     * @brief 通过行键查找 Item数据行
     * @param Key DataTable 行键（FName）
     * @return 数据行指针，未找到则返回 nullptr
     */
    const FItemRow* Find(const FName& Key) const noexcept {
        return ItemMap.Find(Key);
    }

    /** Item数据行 映射表 (RowName -> FItemRow) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|StaticData", meta = (DisplayName = "Item数据行映射表"))
    TMap<FName, FItemRow> ItemMap;

};
