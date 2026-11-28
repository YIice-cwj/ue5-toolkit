# UStaticDataSubsystem API 参考

> **一句话**：按域生成的静态数据唯一持有者，提供冷启动加载、后台热重载、不可变快照读取与版本订阅。
> **头文件**：`Generated/Data/StaticData/UStaticDataSubsystem.h` · **线程**：加载/重载入口在 GameThread 发起，后台预热完成后回 GameThread 原子发布与广播；`GetSnapshot()` / `GetDataVersion()` 可跨线程只读 · **失败语义**：`TGameStatusResult<void>` + `Game::ModuleId::Data` / `Game::DataErrorCodes`（`ReloadInProgress (10012)`、`StagingLoadFailed (10013)`、`ExportDisabledInNonEditor (10014)`）

## 30 秒速览

- 每个业务域一个容器（`FItemStaticData` 等）加一个聚合缓存（`FStaticDataCache`），整体挂在不可变快照 `FStaticDataSnapshot` 上。
- 冷启动默认同步：优先 `.bin`，magic / 版本 / CRC 任一不符或表缺失时整体回落 JSON 通道。
- 热重载恒走后台异步预热，在线快照零触碰；成功后版本号自增并广播。
- 查询固定两步：先 `GetSnapshot()`，再交给 `F{Stem}Query` / `F{Domain}StaticDataInterface`。
- `Deinitialize` 取消在途预热回调并释放重载门闩。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `UStaticDataSubsystem::GetPtr` | 取静态数据子系统指针 | 支持 `UGameInstance`、`UGameInstanceSubsystem` 与任意世界上下文寻址 |
| `UStaticDataSubsystem::LoadAllStaticData` | 冷启动全量加载 | `bAsync = false` 同步完成；首次优先 `.bin`，失败整体回落 JSON |
| `UStaticDataSubsystem::ReloadAllStaticData` | 后台异步热重载 | 恒异步，返回 `Success` 仅表示进入提交流程；重入返回 `ReloadInProgress (10012)` |
| `UStaticDataSubsystem::IsAllDataLoaded` | 查询首次全量加载是否完成 | 重载失败不回退为 `false` |
| `UStaticDataSubsystem::GetSnapshot` | 取得当前在线只读快照 | 可跨线程；首次加载未完成返回空 |
| `UStaticDataSubsystem::GetDataVersion` | 读取数据版本号 | 无锁原子读，自 `0` 起每次成功提交自增 |
| `UStaticDataSubsystem::GetDataVersionObserver` | 订阅版本变更 | 带 `Owner` 的重载必须在 GameThread 调用 |
| `UStaticDataSubsystem::BuildLoadedContentDigest` | 汇总在线快照表数 / 行数 / 摘要 | 未完成首次加载返回零值结构 |
| `UStaticDataSubsystem::ExportStaticDataBin` | 导出运行期 `.bin` | 非编辑器构建返回 `ExportDisabledInNonEditor (10014)` |
| `UStaticDataSubsystem::ReloadAllWorldStaticData` | 向全部在线世界派发热重载 | 对应控制台命令 `Game.ReloadStaticData` |

## 委托与快照类型

```cpp
DECLARE_MULTICAST_DELEGATE_TwoParams(FStaticDataReloadedDelegate, UStaticDataSubsystem*, uint32)

using FStaticDataSnapshotStore = TStaticDataSnapshotStore<FStaticDataSnapshot>;

class GAMEDATASAMPLES_API FStaticDataSnapshot {
public:
    uint32 DataVersion = 0;
    FStaticDataCache Cache;
    FItemStaticData ItemData;
    FShopStaticData ShopData;
};
```

- `OnStaticDataReloaded` 为多播委托，参数为触发重载的子系统与递增后的版本号。
- `FStaticDataSnapshotStore` 是 `GameDataCore` 泛型模板 `TStaticDataSnapshotStore<FStaticDataSnapshot>` 的类型别名，提供无锁 `GetVersion()` 与原子发布。
- 快照发布后只读，禁止拷贝与移动；`DataVersion` 与快照同代绑定。

## 类接口

```cpp
UCLASS(BlueprintType)
class GAMEDATASAMPLES_API UStaticDataSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    void Initialize(FSubsystemCollectionBase& Collection) override;
    void Deinitialize() override;

    [[nodiscard]] TGameStatusResult<void> LoadAllStaticData(bool bAsync = false);

    UFUNCTION(BlueprintPure, Category = "Game|StaticData")
    [[nodiscard]] bool IsAllDataLoaded() const;

    [[nodiscard]] TGameStatusResult<void> ReloadAllStaticData();

    [[nodiscard]] uint32 GetDataVersion() const noexcept;
    [[nodiscard]] TSharedPtr<const FStaticDataSnapshot> GetSnapshot() const;
    [[nodiscard]] FValueObserver<uint32>& GetDataVersionObserver();
    [[nodiscard]] FStaticDataContentDigest BuildLoadedContentDigest() const;

    UFUNCTION(BlueprintPure, Category = "Game|StaticData", meta = (WorldContext = "InWorldContextObject", DisplayName = "Get Static Data Subsystem"))
    [[nodiscard]] static UStaticDataSubsystem* GetPtr(const UObject* InWorldContextObject);

    [[nodiscard]] static UStaticDataSubsystem& Get(const UObject* InWorldContextObject);

    static void ReloadAllWorldStaticData();
    [[nodiscard]] static TGameStatusResult<void> ExportStaticDataBin();

    FStaticDataReloadedDelegate OnStaticDataReloaded;
};
```

## 加载与热重载

```cpp
[[nodiscard]] TGameStatusResult<void> LoadAllStaticData(bool bAsync = false);
[[nodiscard]] TGameStatusResult<void> ReloadAllStaticData();
[[nodiscard]] bool IsAllDataLoaded() const;
```

| 接口 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `LoadAllStaticData` | `bAsync`（默认 `false`） | `TGameStatusResult<void>` | `false` 时同步完成后再返回，保证首个游戏逻辑栈读到完整数据；`true` 走后台预热 |
| `ReloadAllStaticData` | 无 | `TGameStatusResult<void>` | 恒后台异步预热；返回 `Success` 仅表示进入提交流程 |
| `IsAllDataLoaded` | 无 | `bool` | 首次全量加载成功后为 `true`；重载失败不回退 |

- **通道选择**：首次加载优先 `.bin`；`-GameStaticDataChannel=json` 可强制 JSON 通道（CI 双通道对拍）。
- **重载数据源**：热重载直接读磁盘 JSON，不经 `.bin`，避免读到落后于 JSON 的旧档。
- **门闩**：同一时刻仅允许一个写者进入提交流程，重入返回 `ReloadInProgress (10012)`。
- **失败语义**：`.bin` 与 JSON 暂存加载均失败返回 `StagingLoadFailed (10013)`，在线快照保持原状。
- **取消**：`Deinitialize` 经 `FCancellationTokenSource` 取消在途异步预热回调，防止销毁后误提交。

## 快照与版本

```cpp
[[nodiscard]] uint32 GetDataVersion() const noexcept;
[[nodiscard]] TSharedPtr<const FStaticDataSnapshot> GetSnapshot() const;
[[nodiscard]] FValueObserver<uint32>& GetDataVersionObserver();
```

| 接口 | 返回 | 说明 |
| :--- | :--- | :--- |
| `GetDataVersion` | `uint32` | 自 `0` 起，每次成功提交自增；无锁原子读 |
| `GetSnapshot` | `TSharedPtr<const FStaticDataSnapshot>` | 当前在线快照；尚未完成首次加载返回空 |
| `GetDataVersionObserver` | `FValueObserver<uint32>&` | 提交后广播新版本号；`SubscribeWithReplay` 可立即拿到当前值 |

- **稳定性**：读者持有快照共享指针期间，内容不随后续提交改变。
- **行级生命周期**：`F{Stem}Query::FindByKey` 返回的 `TSharedPtr<const Row>` 别名共享快照所有权，可跨版本安全持有。

### 版本订阅示例

```cpp
const TSharedRef<FObserverSubscription> VersionGuard =
    StaticData->GetDataVersionObserver().SubscribeWithReplay(this, [](const uint32& NewVersion) {
        GAME_LOG(Log, TEXT("StaticData version: %u"), NewVersion);
    });
```

- 带 `Owner` 的重载必须在 GameThread 调用；保留返回的订阅守卫，销毁即退订。

## 诊断与导出

```cpp
[[nodiscard]] FStaticDataContentDigest BuildLoadedContentDigest() const;
static void ReloadAllWorldStaticData();
[[nodiscard]] static TGameStatusResult<void> ExportStaticDataBin();
```

- `BuildLoadedContentDigest()`：汇总在线快照的表数、行数与顺序无关摘要，用于 `.bin` / JSON 双通道对拍；未完成首次加载返回零值结构。
- `ExportStaticDataBin()`：将 `DataTables_Json` 全量导出为运行期 `.bin`（Cook 前置）；控制台命令 `Game.ExportStaticDataBin`，无头 Commandlet `UGameStaticDataExportCommandlet -run=GameStaticDataExport`；非编辑器构建返回 `ExportDisabledInNonEditor (10014)`。
- `ReloadAllWorldStaticData()`：向全部在线世界上下文派发热重载，对应控制台命令 `Game.ReloadStaticData`。
- 诊断命令 `Game.ReportStaticDataIntegrity` 打印在线快照表数 / 行数 / SHA-1 摘要；启动参数 `-GameStaticDataVerify` 在 `Initialize` 后写出 `Saved/StaticDataVerify.txt` 并以校验状态码退出。
- `GetPtr` / `Get` 支持 `UGameInstance`、`UGameInstanceSubsystem` 与任意世界上下文对象寻址。

## 典型查询模式

```cpp
UStaticDataSubsystem* StaticData = UStaticDataSubsystem::GetPtr(this);
if (StaticData == nullptr) {
    return;
}

const TSharedPtr<const FStaticDataSnapshot> Snapshot = StaticData->GetSnapshot();
const TSharedPtr<const FItemRow> ItemRow = FItemQuery::FindByKey(Snapshot, FName(TEXT("Item_Potion")));
const TSharedPtr<const FItemCompositeData> ItemComposite =
    FItemStaticDataInterface::FindCompositeData(Snapshot, FName(TEXT("Item_Potion")));
```

## 常见错误与边界

- **重载重入**：已有重载进行中返回 `ReloadInProgress (10012)`，本次请求被忽略，在线数据不变。
- **双通道失败**：`.bin` 不可用且 JSON 装载失败返回 `StagingLoadFailed (10013)`；失败不会清空或替换在线快照。
- **首次加载未完成**：`GetSnapshot()` 返回空，查询门面随之返回空；调用方必须判空。
- **导出限制**：非编辑器构建调用 `ExportStaticDataBin()` 返回 `ExportDisabledInNonEditor (10014)`。
- **快照只读**：不要缓存容器裸指针或引用跨越一次发布；需要长期持有行数据时使用查询返回的共享指针。
- **热重载通道**：重载只读 JSON，不会回退 `.bin`；修改 `.bin` 不会触发热重载。

## 相关文档

- [插件 README](../../README.md)
- [静态数据管线](../Architecture/StaticDataPipeline.md)
- [查询实战](../Guides/StaticDataQueryGuide.md)
- [领域查询 API](DomainQueries.md)
