# GameData 静态数据查询实战指南

本文档介绍在业务逻辑（商城购买、背包整理、掉落生成）中如何安全高效地查询静态数据。

---

## 1. 为什么必须通过快照查询

所有查询接口都要求传入 `TSharedPtr<const FStaticDataSnapshot>`，不提供全局裸数据访问：

```cpp
void UShopManager::ProcessPurchase(FName GoodsKey)
{
    UStaticDataSubsystem* StaticData = UStaticDataSubsystem::Get(this);
    if (!StaticData) return;

    const TSharedPtr<const FStaticDataSnapshot> Snapshot = StaticData->GetSnapshot();
    if (!Snapshot) return;

    const TSharedPtr<const FShopGoodsRow> Goods = FShopGoodsQuery::FindByKey(Snapshot, GoodsKey);
    if (!Goods) return;

    const TSharedPtr<const FItemCompositeData> Item =
        FItemStaticDataInterface::FindCompositeData(Snapshot, Goods->ItemId);
    if (!Item) return;

    // 扣费与发货
}
```

快照机制保证：

- 函数持有旧快照期间，即使后台热重载完成，商品价格与道具数据仍来自同一版本，不会跨表撕裂。
- 行查询返回 `TSharedPtr<const Row>`，以别名方式延长快照生命周期；调用方即使只持有行指针也安全。
- 函数退出后旧快照自然释放，下次调用自动使用最新在线快照。

---

## 2. 三类查询入口

| 入口 | 用途 | 返回 |
| :--- | :--- | :--- |
| `F{Stem}Query::FindByKey(Snapshot, Key)` | 单表主键查询 | `TSharedPtr<const F{Stem}Row>` |
| `F{Domain}StaticDataInterface::FindCompositeData(Snapshot, Key)` | 跨表聚合查询 | `TSharedPtr<const F{Domain}CompositeData>` |
| `F{Domain}CompositeService` | 批量/条件查询等业务扩展 | 由业务扩展定义 |

- `FindByKey` 的键类型随 `row_key_mode`：默认 `FName`；`enum` 模式为 `E{Stem}Key`。
- 每张表首次生成时创建查询脚手架 `F{Stem}Query`，后续重生成不会覆盖；可在其中扩展外键反查、字段过滤、范围查询等该表专属方法。
- 聚合查询按主表全键预热缓存（`FVersionedCache`），发布后只读。

---

## 3. 扩展查询门面

```cpp
// ItemQuery.h（脚手架文件，生成器不会覆盖）
class GAMEDATA_API FItemQuery {
public:
    FItemQuery() = delete;

    static TSharedPtr<const FItemRow> FindByKey(
        const TSharedPtr<const FStaticDataSnapshot>& InSnapshot,
        const FName& InKey);

    /** 按玩法标签过滤（业务自定义扩展） */
    static TArray<TSharedPtr<const FItemRow>> FindByTag(
        const TSharedPtr<const FStaticDataSnapshot>& InSnapshot,
        const FGameplayTag& InTag);
};
```

扩展方法内部同样只读取传入快照，不得缓存跨版本的裸指针或引用。

---

## 4. 数据版本订阅

需要感知配表热更（例如刷新 UI 缓存）时：

```cpp
FItemRowView::FItemRowView(UStaticDataSubsystem* InStaticData)
    : StaticData(InStaticData)
{
    VersionSubscription = StaticData->GetDataVersionObserver().SubscribeWithReplay(
        [this](const uint32&) { RefreshFromLatestSnapshot(); });
}
```

- `SubscribeWithReplay` 订阅时立即回调当前版本，之后每次提交再回调。
- 解引用前确认 `StaticData` 生命周期；观察者持有者销毁时退订（`FObserverSubscription` RAII）。

---

## 5. 常见错误

| 错误 | 后果 | 正确做法 |
| :--- | :--- | :--- |
| 缓存 `const FItemRow*` 到成员变量 | 热更后悬垂 | 缓存 `TSharedPtr<const FItemRow>`，或缓存键并在用时重查 |
| 跨逻辑栈复用同一 `Snapshot` | 读到过期数据 | 每个逻辑入口重新 `GetSnapshot()` |
| 在非主线程查询 | 未定义行为 | 查询只在游戏线程进行 |
| 用 `enum` 键模式承载超过 255 行 | 生成期报错 | 改用 `row_key_mode = "fname"` |
