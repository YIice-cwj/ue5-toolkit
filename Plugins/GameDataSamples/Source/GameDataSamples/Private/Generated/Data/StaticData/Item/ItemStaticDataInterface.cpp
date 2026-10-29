#include "Generated/Data/StaticData/Item/ItemStaticDataInterface.h"

#include "Generated/Data/StaticData/FStaticDataCache.h"
#include "Generated/Data/StaticData/FStaticDataSnapshot.h"

/**
 * @brief 获取单体聚合数据（FName 键）
 * @param InSnapshot 静态数据快照（由 UStaticDataSubsystem::GetSnapshot 取得）
 * @param InKey 唯一索引键
 * @return 聚合数据快照（引用计数兜底，可跨版本持有），未命中或快照为空返回空
 *
 * 实现思路：直接查快照内该域聚合缓存（缓存由 WarmUpCompositeCache 在提交前预热）
 */
TSharedPtr<const FItemCompositeData> FItemStaticDataInterface::FindCompositeData(const TSharedPtr<const FStaticDataSnapshot>& InSnapshot, const FName& InKey) {
    if (!InSnapshot.IsValid()) {
        return nullptr;
    }
    return InSnapshot->Cache.ItemComposites().FindShared(InKey);
}

/**
 * @brief 预热聚合缓存
 * @param InCache 聚合缓存
 * @param InData 域静态数据容器
 *
 * 实现思路：收集主表全部键并逐键构建聚合数据；跨表引用完整性由数据校验管线负责，
 * 策略判定收敛在缓存内部
 */
void FItemStaticDataInterface::WarmUpCompositeCache(FStaticDataCache& InCache, const FItemStaticData& InData) {
    TArray<FName> PrimaryKeys;
    InData.ItemMap.GenerateKeyArray(PrimaryKeys);
    InCache.ItemComposites().Rebuild(MoveTemp(PrimaryKeys), [&InData](FName Key) -> FItemCompositeData {
        FItemCompositeData Composite;
        if (const FItemRow* Row = InData.ItemMap.Find(Key)) {
            Composite.Item = *Row;
        }
        return Composite;
    });
}
