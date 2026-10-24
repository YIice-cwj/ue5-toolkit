# GameData 存档子系统 API 参考

`USaveGameSubsystem`（`GameDataCore`）是零业务字段的通用分段存档调度器。

头文件：`#include "Persistence/USaveGameSubsystem.h"`

---

## 1. 数据结构

### 1.1 `ESaveGameResult`

```cpp
UENUM(BlueprintType)
enum class ESaveGameResult : uint8 {
    Success,
    IoError,
    CorruptedData,
    VersionMismatch,
    SlotNotFound
};
```

### 1.2 `FGameSaveSegment`

```cpp
struct FGameSaveSegment {
    FName SegmentName;
    int32 SegmentVersion = 1;
    TFunction<void(FArchive&)> Save;
    TFunction<bool(FArchive&, int32 StoredVersion)> Load;
};
```

### 1.3 `UGameSaveArchive`

```cpp
UCLASS(BlueprintType)
class GAMEDATACORE_API UGameSaveArchive : public USaveGame {
    static constexpr int32 CURRENT_ARCHIVE_VERSION = 1;

    FGameSaveSlotMetadata Metadata;
    TMap<FName, FGameSaveSegmentPayload> Segments;
};
```

---

## 2. 子系统接口

```cpp
UCLASS(BlueprintType)
class GAMEDATACORE_API USaveGameSubsystem : public UGameInstanceSubsystem {
public:
    void SetSaveGameClass(TSubclassOf<UGameSaveArchive> InSaveGameClass);
    TSubclassOf<UGameSaveArchive> GetSaveGameClass() const;

    void RegisterSegment(const FGameSaveSegment& Segment);
    bool UnregisterSegment(FName SegmentName);
    bool HasSegment(FName SegmentName) const;

    void SetAllowedSlotNames(const TArray<FString>& InAllowedSlotNames);
    bool IsSlotNameAllowed(const FString& SlotName) const;

    bool SaveGameToSlot(const FString& SlotName);
    bool SaveGameToSlotAsync(const FString& SlotName);
    bool LoadGameFromSlot(const FString& SlotName);
    bool LoadGameFromSlotAsync(const FString& SlotName);

    bool SaveObjectToSlot(const FString& SlotName, USaveGame* SaveGameObject);
    bool LoadObjectFromSlot(const FString& SlotName, TSubclassOf<USaveGame> SaveGameClass, USaveGame*& OutSaveGame);

    bool DoesSaveSlotExist(const FString& SlotName) const;
    bool DeleteSaveSlot(const FString& SlotName);

    static FString GetSaveSlotFilePath(const FString& SlotName);
    static FString GetSaveSlotTempFilePath(const FString& SlotName);

    FOnAsyncSaveComplete OnSaveComplete;
    FOnAsyncLoadComplete OnLoadComplete;
};
```

---

## 3. 语义说明

| 接口 | 语义 |
| :--- | :--- |
| `RegisterSegment` | 按段名覆盖注册；段名、保存委托、加载委托缺一不可 |
| `SaveGameToSlot` | 主线程逐段捕获并原子写盘 |
| `SaveGameToSlotAsync` | 主线程捕获段字节，后台写盘；完成后广播 `OnSaveComplete` |
| `LoadGameFromSlot` | 同步读取并逐段应用；损坏档返回 `false` |
| `LoadGameFromSlotAsync` | 后台读盘，回主线程反序列化并逐段应用 |
| `SaveObjectToSlot` | 任意 `USaveGame` 对象的存取逃生口，不走段注册 |
| `DoesSaveSlotExist` | 正式 `.sav` 或可抢救的 `.sav.tmp` 存在即返回 true |
| `DeleteSaveSlot` | 删除正式档并清理临时档；同时使代次失效（置为 MAX_uint64），彻底阻断在途异步写任务复活已删除槽位 |

`OnSaveComplete` / `OnLoadComplete` 仅异步路径广播；同步路径由返回值判定。

---

## 4. 槽位校验

`IsSlotNameAllowed` 规则：

1. 非空且长度不超过 64；
2. 不含 `/`、`\`、`.`；
3. 白名单非空时必须命中白名单。

不合法槽位在保存/加载/删除入口直接拒绝并记录 Warning。

---

## 5. 损坏档防护与灾备恢复机制

`ReadSlotBytes` 在进入引擎反序列化前校验：

- 文件类型 tag（`0x53415647`，即 `"SAVG"`）；
- 最小头长度 64 字节。

**灾备恢复与断电保护机制**：
1. **自动临时档抢救**：若正式档 `.sav` 损坏、截断或缺失，自动回退读取未完成移动的 `.sav.tmp` 副本。若临时副本格式合法，自动反序列化恢复数据并输出 Warning，最大程度挽回掉电导致的数据损失；
2. **移动失败保留副本**：`WriteSlotBytes` 中若原子移动失败，不再直接删除临时文件，保留现场以便灾备抢救与排查；
3. **分段保护**：在分段归档时，若外部注册系统或对象指针失效，主动调用 `Ar.SetError()` 终止写入，杜绝向磁盘写入空白数据导致旧存档被覆盖清空。
