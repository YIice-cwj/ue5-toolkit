# GameData 领域查询与复合数据 API 参考

数据表生成管线按 Excel 所在目录自动划分业务域，并为每张表生成强类型查询门面 `F{Stem}Query`，为每个域生成聚合数据与接口 `F{Domain}CompositeData` / `F{Domain}StaticDataInterface`。

> 本仓库内置的 `Item` / `Shop` 域位于示例插件 `GameDataSamples`；目标工程安装后，生成代码写入 `GameData` 模块，命名随表名与目录变化。

---

## 1. 行查询 `F{Stem}Query`

```cpp
class GAMEDATA_API FItemQuery {
public:
    FItemQuery() = delete;

    static TSharedPtr<const FItemRow> FindByKey(
        const TSharedPtr<const FStaticDataSnapshot>& InSnapshot,
        const FName& InKey
    );
};
```

- 返回持快照所有权的行共享指针；未命中或快照为空返回空。
- 调用方无需额外持有 `Snapshot`，行数据在跨版本期间安全。
- 生成器首次创建查询脚手架后不再覆盖，可在其中扩展外键反查、字段过滤、范围查询等该表专属方法。

---

## 2. 聚合数据与接口

每张分表对应一个 `F{Domain}CompositeData`，每个域对应一个缓存与接口：

```cpp
USTRUCT(BlueprintType)
struct GAMEDATA_API FItemCompositeData {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FItemRow Item;
};

class GAMEDATA_API FItemStaticDataInterface {
public:
    FItemStaticDataInterface() = delete;

    static TSharedPtr<const FItemCompositeData> FindCompositeData(
        const TSharedPtr<const FStaticDataSnapshot>& InSnapshot,
        const FName& InKey
    );

    static void WarmUpCompositeCache(FStaticDataCache& InCache, const FItemStaticData& InData);
};
```

- 聚合缓存按主表全键预热；跨表引用完整性由数据校验管线负责。
- 缓存实现为 `FVersionedCache`，快照提交前预热，发布后只读。

---

## 3. 生成域的示例

以 Item 为例（`GameDataSamples`）：

```cpp
#include "Generated/Data/StaticData/Item/ItemQuery.h"
#include "Generated/Data/StaticData/Item/ItemStaticDataInterface.h"

TSharedPtr<const FStaticDataSnapshot> Snapshot = StaticDataSubsystem->GetSnapshot();
const TSharedPtr<const FItemRow> Row = FItemQuery::FindByKey(Snapshot, FName(TEXT("1001")));
const TSharedPtr<const FItemCompositeData> Composite = FItemStaticDataInterface::FindCompositeData(Snapshot, FName(TEXT("1001")));
```

---

## 4. 键模式

- `row_key_mode = "fname"`（默认）：`FindByKey` 使用 `FName`，行数不受 255 限制。
- `row_key_mode = "enum"`：生成 `E{Stem}Key : uint8` 枚举键；超过 255 行会在生成期明确报错，应改用 `fname`。
