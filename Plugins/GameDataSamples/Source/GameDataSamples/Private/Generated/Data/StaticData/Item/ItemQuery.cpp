// Automatically generated initial query implementation for Item.
// This file will NOT be overwritten by subsequent script runs.
#include "Generated/Data/StaticData/Item/ItemQuery.h"

/**
 * @brief 主键查询（返回持快照所有权的行共享指针）
 * @param InSnapshot 静态数据快照（由 UStaticDataSubsystem::GetSnapshot 取得）
 * @param InKey 唯一键
 * @return 行共享指针（别名共享快照所有权），未命中或快照为空返回空
 *
 * 实现思路：以别名共享指针把快照所有权挂到行指针上，调用方无需额外持有快照即可安全跨版本使用
 */
TSharedPtr<const FItemRow> FItemQuery::FindByKey(const TSharedPtr<const FStaticDataSnapshot>& InSnapshot, const FName& InKey) {
    if (!InSnapshot.IsValid()) {
        return nullptr;
    }
    const FItemRow* Row = InSnapshot->ItemData.ItemMap.Find(InKey);
    return Row != nullptr ? TSharedPtr<const FItemRow>(InSnapshot, Row) : nullptr;
}
