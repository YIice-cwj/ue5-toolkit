/**
 * @file ShopStaticData.h
 * @brief Shop 静态数据容器
 */
#pragma once

#include "CoreMinimal.h"

#include "Misc/Paths.h"
#include "StaticData/FStaticDataRepository.h"
#include "Types/TGameResult.h"
#include "Utils/DataTableLoaderUtils.h"
#include "Generated/DataTables/Shop/ShopDataTableRowTypes.h"
#include "ShopStaticData.generated.h"

/**
 * @brief Shop 静态数据容器（存储该模块所有分表的 Map，由 FStaticDataSnapshot 只读持有）
 */
USTRUCT(BlueprintType)
struct GAMEDATASAMPLES_API FShopStaticData {
    GENERATED_BODY()

public:
    /**
     * @brief 从指定 JSON 目录加载当前模块所有分表数据
     * @param InJsonDir JSON 所在根目录，缺省自动使用 [项目目录]/DataTables_Json/Shop
     * @return 全部加载成功返回 Success，任一分表失败透传底层 Game::DataErrorCodes 失败结果
     */
    [[nodiscard]] TGameStatusResult<void> LoadAllFromJson(const FString& InJsonDir = TEXT("")) {
        const FString BaseDir = InJsonDir.IsEmpty()
            ? (FPaths::ProjectDir() / TEXT("DataTables_Json") / TEXT("Shop"))
            : InJsonDir;

        auto LoadRes_Goods = FDataTableLoaderUtils::LoadTableFromJson<FShopGoodsRow>(BaseDir / TEXT("DT_ShopGoods.json"));
        if (LoadRes_Goods.IsFailure()) {
            return TGameStatusResult<void>::Failure(LoadRes_Goods.GetError());
        }
        GoodsMap = LoadRes_Goods.ExtractValue();
        return TGameStatusResult<void>::Success();
    }

    /**
     * @brief 从生成式二进制仓库整体装载当前模块所有分表数据
     * @param Repository 已登记读写器并完成 .bin 装载的仓库
     * @return 全部提取成功返回 Success，任一分表失败透传底层 Game::DataErrorCodes 失败结果
     */
    [[nodiscard]] TGameStatusResult<void> LoadAllFromRepository(const FStaticDataRepository& Repository) {
        auto ExtractRes_Goods = Repository.ExtractTable<FShopGoodsRow>();
        if (ExtractRes_Goods.IsFailure()) {
            return TGameStatusResult<void>::Failure(ExtractRes_Goods.GetError());
        }
        GoodsMap = ExtractRes_Goods.ExtractValue();
        return TGameStatusResult<void>::Success();
    }

    /**
     * @brief 将当前模块所有分表数据写入生成式二进制仓库
     * @param Repository 目标仓库（须已登记读写器）
     * @return 全部写入成功返回 Success，任一分表失败透传底层 Game::DataErrorCodes 失败结果
     */
    [[nodiscard]] TGameStatusResult<void> AddAllToRepository(FStaticDataRepository& Repository) const {
        if (const TGameStatusResult<void> AddRes_Goods = Repository.AddTable(GoodsMap); AddRes_Goods.IsFailure()) {
            return AddRes_Goods;
        }
        return TGameStatusResult<void>::Success();
    }

    /**
     * @brief 通过行键查找 ShopGoods数据行
     * @param Key DataTable 行键（FName）
     * @return 数据行指针，未找到则返回 nullptr
     */
    const FShopGoodsRow* Find(const FName& Key) const noexcept {
        return GoodsMap.Find(Key);
    }

    /**
     * @brief 按分表限定名与行键查找 ShopGoods数据行
     * @param Key DataTable 行键（FName）
     * @return 数据行指针，未找到则返回 nullptr
     */
    const FShopGoodsRow* FindGoods(const FName& Key) const noexcept {
        return GoodsMap.Find(Key);
    }

    /**
     * @brief ShopGoods数据行 映射表 (RowName -> FShopGoodsRow)
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop|StaticData", meta = (DisplayName = "ShopGoods数据行映射表"))
    TMap<FName, FShopGoodsRow> GoodsMap;

};
