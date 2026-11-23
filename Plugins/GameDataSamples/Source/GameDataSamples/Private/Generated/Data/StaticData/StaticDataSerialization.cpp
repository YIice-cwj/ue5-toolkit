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
 * @brief FItemRow 行负载读写（生成式免反射，首部含字段结构签名校验，字段顺序即负载布局）
 * @param Ar 归档（按 IsSaving 分流读写）
 * @param RowPtr 行结构体指针
 * @return 归档无错且结构签名匹配返回 true
 */
bool SerializeFItemRow(FArchive& Ar, void* RowPtr) {
    FItemRow& Row = *static_cast<FItemRow*>(RowPtr);
    uint32 SchemaHash = 0x9C8C2B0Fu;
    Ar << SchemaHash;
    if (Ar.IsLoading() && (Ar.IsError() || SchemaHash != 0x9C8C2B0Fu)) {
        Ar.SetError();
        return false;
    }
    Ar << Row.ItemName;
    Ar << Row.BaseAttack;
    Ar << Row.GoldCost;
    Ar << Row.bIsUsable;
    Ar << Row.ItemTag;
    return !Ar.IsError();
}

/**
 * @brief FShopGoodsRow 行负载读写（生成式免反射，首部含字段结构签名校验，字段顺序即负载布局）
 * @param Ar 归档（按 IsSaving 分流读写）
 * @param RowPtr 行结构体指针
 * @return 归档无错且结构签名匹配返回 true
 */
bool SerializeFShopGoodsRow(FArchive& Ar, void* RowPtr) {
    FShopGoodsRow& Row = *static_cast<FShopGoodsRow*>(RowPtr);
    uint32 SchemaHash = 0x62AD9C24u;
    Ar << SchemaHash;
    if (Ar.IsLoading() && (Ar.IsError() || SchemaHash != 0x62AD9C24u)) {
        Ar.SetError();
        return false;
    }
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
 */
void RegisterGameStaticDataRowSerializers(FStaticDataRepository& Repository) {
    Repository.RegisterRowSerializer<FItemRow>(&SerializeFItemRow);
    Repository.RegisterRowSerializer<FShopGoodsRow>(&SerializeFShopGoodsRow);
}
