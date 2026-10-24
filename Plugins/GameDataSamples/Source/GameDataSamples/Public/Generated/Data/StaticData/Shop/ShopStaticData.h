// Automatically generated static data container for Shop. Do not edit manually.
#pragma once

#include "CoreMinimal.h"

#include "Misc/Paths.h"
#include "Utils/DataTableLoaderUtils.h"
#include "Data/StaticData/FStaticDataRepository.h"
#include "Generated/DataTables/Shop/ShopDataTableRowTypes.h"
#include "ShopCompositeData.h"
#include "ShopStaticData.generated.h"

/**
 * @brief Shop 静态数据容器（存储该模块所有分表的 Map，由 UStaticDataSubsystem 持有）
 */
USTRUCT(BlueprintType)
struct GAMEDATASAMPLES_API FShopStaticData {
    GENERATED_BODY()

public:
    /**
     * @brief 从指定 JSON 目录加载当前模块所有分表数据
     * @param InJsonDir JSON 所在根目录，缺省自动使用 [项目目录]/DataTables_Json/Shop
     * @return 是否所有分表均加载成功
     */
    bool LoadAllFromJson(const FString& InJsonDir = TEXT("")) {
        const FString BaseDir = InJsonDir.IsEmpty()
            ? (FPaths::ProjectDir() / TEXT("DataTables_Json") / TEXT("Shop"))
            : InJsonDir;

        bool bAllSuccess = true;
        bAllSuccess &= FDataTableLoaderUtils::LoadTableFromJson(BaseDir / TEXT("DT_ShopGoods.json"), GoodsMap);
        return bAllSuccess;
    }

    /**
     * @brief 从生成式二进制仓库整体装载当前模块所有分表数据
     * @param Repository 已登记读写器并完成 .bin 装载的仓库
     * @return 是否所有分表均提取成功（失败时可用仓库错误码定位原因）
     */
    bool LoadAllFromRepository(const FStaticDataRepository& Repository) {
        bool bAllSuccess = true;
        bAllSuccess &= Repository.ExtractTable(GoodsMap);
        return bAllSuccess;
    }

    /**
     * @brief 将当前模块所有分表数据写入生成式二进制仓库
     * @param Repository 目标仓库（须已登记读写器）
     * @return 是否所有分表均写入成功（失败时可用仓库错误码定位原因）
     */
    bool AddAllToRepository(FStaticDataRepository& Repository) const {
        bool bAllSuccess = true;
        bAllSuccess &= Repository.AddTable(GoodsMap);
        return bAllSuccess;
    }

    /**
     * @brief 通过行键查找 ShopGoods数据行
     * @param Key DataTable 行键（FName）
     * @return 数据行指针，未找到则返回 nullptr
     */
    const FShopGoodsRow* Find(const FName& Key) const noexcept {
        return GoodsMap.Find(Key);
    }

    /** ShopGoods数据行 映射表 (RowName -> FShopGoodsRow) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop|StaticData", meta = (DisplayName = "ShopGoods数据行映射表"))
    TMap<FName, FShopGoodsRow> GoodsMap;

};
