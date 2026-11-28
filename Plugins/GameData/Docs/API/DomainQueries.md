# GameData 领域查询 API 参考

> **一句话**：数据表生成管线产出的强类型行查询 `F{Stem}Query`、域聚合载具 `F{Domain}CompositeData` 与业务门面 `F{Domain}StaticDataInterface`。
> **头文件**：`Generated/Data/StaticData/{Domain}/{Stem}Query.h` 等生成头文件 · **线程**：查询是只读纯函数，可并发调用；快照需经 `UStaticDataSubsystem::GetSnapshot()` 取得 · **失败语义**：未命中返回空共享指针，装载失败由 `TGameStatusResult` + `Game::DataErrorCodes` 承载

## 30 秒速览

- 每张表生成 `F{Stem}Query::FindByKey`，返回 `TSharedPtr<const F{Stem}Row>`，别名共享快照所有权，可跨版本安全持有。
- 每个业务域生成 `F{Domain}CompositeData`（`USTRUCT`，汇集该域各分表单行）与 `F{Domain}StaticDataInterface`（`FindCompositeData` / `WarmUpCompositeCache`）。
- 示例域 `Item` / `Shop` 位于 `GameDataSamples`；安装到目标工程后，生成代码写入 `GameData` 模块，命名随表名与目录变化。
- 键模式默认 `fname`；选择 `enum` 时生成 `E{Stem}Key : uint8`，含 `None = 0` 共 255 个枚举项，最多支持 254 行有效数据。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `UStaticDataSubsystem::GetSnapshot` | 取得当前在线只读快照 | 原子发布，须经此获取以保持版本一致 |
| `F{Stem}Query::FindByKey` | 按行键查强类型表行 | 只读可并发；未命中或快照为空返回空 |
| `F{Domain}StaticDataInterface::FindCompositeData` | 取域聚合数据 | 只读可并发；未命中或快照为空返回空 |
| `F{Domain}StaticDataInterface::WarmUpCompositeCache` | 预热域聚合缓存 | 仅在加载 / 热更提交链路、快照发布前调用 |
| `F{Domain}StaticData::Find` | 按枚举键取行裸指针 | 仅该快照存活期间有效，不持所有权 |
| `ToRowName` | 枚举键转行名 | 仅 `enum` 键模式下生成 |

## 行查询 `F{Stem}Query`

```cpp
class GAMEDATASAMPLES_API FItemQuery {
public:
    FItemQuery() = delete;

    static TSharedPtr<const FItemRow> FindByKey(const TSharedPtr<const FStaticDataSnapshot>& InSnapshot, const FName& InKey);
};
```

| 参数 | 类型 | 说明 |
| :--- | :--- | :--- |
| `InSnapshot` | `const TSharedPtr<const FStaticDataSnapshot>&` | 由 `UStaticDataSubsystem::GetSnapshot()` 取得的快照 |
| `InKey` | `const FName&` | 行唯一键 |

| 返回 | 说明 |
| :--- | :--- |
| `TSharedPtr<const FItemRow>` | 持快照所有权的行数据；未命中或快照为空返回空 |

- **线程**：查询只读，可并发调用；不要写入快照内的任何容器。
- **生命周期**：返回的共享指针在快照存活期间有效，调用方无需额外持有 `Snapshot`。
- **扩展**：`F{Stem}Query` 首次由生成脚本创建，后续重新生成数据表时不会覆盖，可在其中扩展外键反查、字段过滤、范围查询等表专属方法。

## 聚合数据与业务门面

```cpp
USTRUCT(BlueprintType)
struct GAMEDATASAMPLES_API FItemCompositeData {
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Composite", meta = (DisplayName = "Item数据行"))
    FItemRow Item;
};

class GAMEDATASAMPLES_API FItemStaticDataInterface {
public:
    FItemStaticDataInterface() = delete;

    static TSharedPtr<const FItemCompositeData> FindCompositeData(const TSharedPtr<const FStaticDataSnapshot>& InSnapshot, const FName& InKey);

    static void WarmUpCompositeCache(FStaticDataCache& InCache, const FItemStaticData& InData);
};
```

### `FindCompositeData`

| 参数 | 类型 | 说明 |
| :--- | :--- | :--- |
| `InSnapshot` | `const TSharedPtr<const FStaticDataSnapshot>&` | 当前在线快照 |
| `InKey` | `const FName&` | 主表行键 |

| 返回 | 说明 |
| :--- | :--- |
| `TSharedPtr<const FItemCompositeData>` | 聚合数据只读共享指针；未命中或快照为空返回空 |

### `WarmUpCompositeCache`

| 参数 | 类型 | 说明 |
| :--- | :--- | :--- |
| `InCache` | `FStaticDataCache&` | 挂接于快照的聚合缓存 |
| `InData` | `const FItemStaticData&` | 该域行表容器 |

- **缓存**：聚合缓存实现为 `FVersionedCache`，按主表全键预热；快照提交前完成预热，发布后只读。
- **线程**：`FindCompositeData` 只读可并发；`WarmUpCompositeCache` 仅在加载/热更提交链路、快照发布前调用。
- **完整性**：跨表引用完整性由数据校验管线负责，查询阶段不做二次校验。

### 最小示例

```cpp
#include "Generated/Data/StaticData/Item/ItemQuery.h"
#include "Generated/Data/StaticData/Item/ItemStaticDataInterface.h"

UStaticDataSubsystem* StaticData = UStaticDataSubsystem::GetPtr(this);
const TSharedPtr<const FStaticDataSnapshot> Snapshot = StaticData->GetSnapshot();
const TSharedPtr<const FItemRow> Row = FItemQuery::FindByKey(Snapshot, FName(TEXT("Item_Sword")));
const TSharedPtr<const FItemCompositeData> Composite =
    FItemStaticDataInterface::FindCompositeData(Snapshot, FName(TEXT("Item_Sword")));
```

## 键模式

- `row_key_mode = "fname"`（默认）：`FindByKey` 使用 `FName`，行数不受限制。
- `row_key_mode = "enum"`：额外生成 `E{Stem}Key : uint8`、`inline FName ToRowName(E{Stem}Key InKey) noexcept`，并在 `F{Domain}StaticData` 上生成 `const F{Stem}Row* Find(E{Stem}Key InKey) const noexcept`。
- 枚举含 `None = 0`，共 255 个枚举项；有效数据超过 254 行时生成期直接报错，应改用 `fname`。

## 常见错误与边界

- **快照为空**：尚未完成首次加载时 `FindByKey` / `FindCompositeData` 返回空，不崩溃；调用方需判空。
- **裸指针不持所有权**：`F{Domain}StaticData::Find` 返回 `const F{Stem}Row*`，仅在该快照存活期间有效；需要跨版本持有时改用 `F{Stem}Query::FindByKey`。
- **不要自建快照**：快照由 `UStaticDataSubsystem` 原子发布，绕过 `GetSnapshot()` 读取会失去版本一致性。
- **枚举键容量**：`enum` 模式最多 254 行有效数据，超过即生成失败，不是运行期告警。
- **生成文件不覆盖**：`F{Stem}Query` 与 `F{Domain}StaticDataInterface` 首次创建后不再被生成器覆盖，手写扩展不会被冲掉。

## 相关文档

- [插件 README](../../README.md)
- [静态数据管线](../Architecture/StaticDataPipeline.md)
- [查询实战](../Guides/StaticDataQueryGuide.md)
- [快速上手](../Guides/QuickStart.md)
