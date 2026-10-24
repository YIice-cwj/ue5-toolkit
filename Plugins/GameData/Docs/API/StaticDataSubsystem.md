# GameData 静态数据子系统 API 参考

`UStaticDataSubsystem` 由数据表生成管线按域生成，继承 `UGameInstanceSubsystem`，是运行期静态配置数据的唯一持有者。所有查询都先取快照，再交给各域查询门面。

头文件：`#include "Generated/Data/StaticData/UStaticDataSubsystem.h"`

> 生成代码写入 `GameData` 模块；示例实现见 `GameDataSamples/Public/Generated/Data/StaticData/UStaticDataSubsystem.h`。

---

## 1. 委托声明

```cpp
DECLARE_MULTICAST_DELEGATE_TwoParams(
    FStaticDataReloadedDelegate,
    UStaticDataSubsystem* /*Subsystem*/,
    uint32 /*NewVersion*/
);
```

---

## 2. 类接口

```cpp
UCLASS(BlueprintType)
class GAMEDATA_API UStaticDataSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    bool LoadAllStaticData(bool bAsync = false);

    UFUNCTION(BlueprintPure, Category = "Game|StaticData")
    bool IsAllDataLoaded() const;

    bool ReloadAllStaticData();

    [[nodiscard]] uint32 GetDataVersion() const noexcept;
    [[nodiscard]] TSharedPtr<const FStaticDataSnapshot> GetSnapshot() const;
    [[nodiscard]] FValueObserver<uint32>& GetDataVersionObserver();
    [[nodiscard]] FStaticDataContentDigest BuildLoadedContentDigest() const;

    UFUNCTION(BlueprintPure, Category = "Game|StaticData", meta = (WorldContext = "InWorldContextObject"))
    static UStaticDataSubsystem* Get(const UObject* InWorldContextObject);

    static void ReloadAllWorldStaticData();
    static bool ExportStaticDataBin();

    FStaticDataReloadedDelegate OnStaticDataReloaded;
};
```

---

## 3. 接口语义

### 3.1 `LoadAllStaticData(bool bAsync = false)`
- 冷启动走 `bAsync = false`，同步完成后再返回，保证首个游戏逻辑栈即可读到完整数据。
- 首次加载优先 `.bin` 通道；magic / 格式版本 / CRC 任一不符或表缺失时整体回落 JSON 通道。
- 命令行 `-GameStaticDataChannel=json` 可强制 JSON 通道（CI 双通道对拍）。
- 返回暂存数据是否装载成功并已就绪或进入后台预热。

### 3.2 `ReloadAllStaticData()`
- 恒走后台异步预热，在线快照零触碰；成功后版本号自增并广播。
- 数据源为磁盘 JSON，不经 `.bin`，避免读到落后于 JSON 的旧档。
- 失败时保留旧快照，业务侧无感。

### 3.3 `GetSnapshot()`
- 返回当前在线快照的共享指针；尚未完成首次加载时为空。
- 读者在该快照存活期间看到的内容绝对稳定，不随后续提交改变。
- 行查询返回 `TSharedPtr<const Row>`（别名共享快照所有权），可跨版本安全持有。

### 3.4 版本与订阅
- `GetDataVersion()` 自 0 起，每次成功提交自增。
- `GetDataVersionObserver()` 为 `FValueObserver<uint32>`，提交后广播新版本号；支持 `SubscribeWithReplay` 立即拿到当前值。
- `OnStaticDataReloaded` 为多播委托，参数为触发重载的子系统与递增后的版本号。

### 3.5 诊断与工具
- `BuildLoadedContentDigest()`：汇总在线快照的表数、行数与顺序无关摘要，用于 `.bin` / JSON 双通道对拍。
- `ExportStaticDataBin()`：将 `DataTables_Json` 全量导出为运行期 `.bin`（Cook 前置；控制台命令 `Game.ExportStaticDataBin`）。
- `ReloadAllWorldStaticData()`：向全部在线世界上下文派发热重载。
- `Get(WorldContextObject)`：通过任意上下文对象取子系统，不存在返回 `nullptr`。

---

## 4. 典型查询模式

```cpp
UStaticDataSubsystem* StaticData = UStaticDataSubsystem::Get(this);
if (!StaticData) return;

const TSharedPtr<const FStaticDataSnapshot> Snapshot = StaticData->GetSnapshot();
const TSharedPtr<const FItemRow> ItemRow = FItemQuery::FindByKey(Snapshot, FName(TEXT("1001")));
const TSharedPtr<const FItemCompositeData> ItemComposite =
    FItemStaticDataInterface::FindCompositeData(Snapshot, FName(TEXT("1001")));
```
