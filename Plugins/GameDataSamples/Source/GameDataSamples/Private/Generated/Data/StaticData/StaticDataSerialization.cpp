/**
 * @file StaticDataSerialization.cpp
 * @brief 静态数据行序列化器实现
 */

#include "Generated/Data/StaticData/StaticDataSerialization.h"

#include "GameplayTagContainer.h"

#include "Generated/DataTables/Item/ItemRow.h"
#include "Generated/DataTables/Shop/ShopGoodsRow.h"

namespace {

/**
 * @brief FItemRow 行负载读写（生成式免反射，字段顺序即负载布局）
 * @param Ar 归档（按 IsSaving 分流读写）
 * @param RowPtr 行结构体指针
 * @return 归档无错返回 true
 */
bool SerializeFItemRow(FArchive& Ar, void* RowPtr) {
    FItemRow& Row = *static_cast<FItemRow*>(RowPtr);
    Ar << Row.ItemName;
    Ar << Row.BaseAttack;
    Ar << Row.GoldCost;
    Ar << Row.bIsUsable;
    Ar << Row.ItemTag;
    return !Ar.IsError();
}

/**
 * @brief FShopGoodsRow 行负载读写（生成式免反射，字段顺序即负载布局）
 * @param Ar 归档（按 IsSaving 分流读写）
 * @param RowPtr 行结构体指针
 * @return 归档无错返回 true
 */
bool SerializeFShopGoodsRow(FArchive& Ar, void* RowPtr) {
    FShopGoodsRow& Row = *static_cast<FShopGoodsRow*>(RowPtr);
    Ar << Row.GoodsName;
    Ar << Row.ItemId;
    Ar << Row.DisplayItemId;
    Ar << Row.Price;
    return !Ar.IsError();
}

}  // namespace

/**
 * @brief 注册全部生成式行负载读写器（免反射，供 FStaticDataRepository 按结构名调度）
 * @param Repository 目标仓库
 *
 * 实现思路：逐表把「结构名 -> 读写器」登记入仓库；结构名直接取 RowStruct 的 StaticStruct 名，
 * 与 FStaticDataRepository 模板入口的 RowType::StaticStruct()->GetName() 同源，避免名称字面量漂移。
 */
void RegisterGameStaticDataRowSerializers(FStaticDataRepository& Repository) {
    Repository.RegisterRowSerializer(FName(FItemRow::StaticStruct()->GetName()), &SerializeFItemRow);
    Repository.RegisterRowSerializer(FName(FShopGoodsRow::StaticStruct()->GetName()), &SerializeFShopGoodsRow);
}
