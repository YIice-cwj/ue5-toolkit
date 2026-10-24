// Automatically generated initial query scaffold for Item.
// This file will NOT be overwritten by subsequent script runs.
#pragma once

#include "CoreMinimal.h"

#include "Generated/DataTables/Item/ItemRow.h"
#include "Generated/Data/StaticData/FStaticDataSnapshot.h"

/**
 * @brief Item数据行 表级查询（纯静态方法，取数专用）
 * @note 此文件首次由生成脚本创建，后续重新生成数据表时【绝不会被覆盖】。
 *       开发者可在此扩展该表专属辅助查询（外键反查、字段过滤、范围查询等）。
 */
class GAMEDATASAMPLES_API FItemQuery {
public:
    FItemQuery() = delete;

    /**
     * @brief 主键查询（返回持快照所有权的行共享指针，可跨版本安全持有）
     * @param InSnapshot 静态数据快照（由 UStaticDataSubsystem::GetSnapshot 取得）
     * @param InKey 唯一键
     * @return 行共享指针（别名共享快照所有权），未命中或快照为空返回空
     */
    static TSharedPtr<const FItemRow> FindByKey(const TSharedPtr<const FStaticDataSnapshot>& InSnapshot, const FName& InKey);
};
