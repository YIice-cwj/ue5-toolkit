# GameData 存档子系统 API 参考

`USaveGameSubsystem`（`GameDataCore`）是零业务字段的通用分段存档调度器。

头文件：`#include "Save/USaveGameSubsystem.h"`

---

## 1. 数据结构

### 1.1 `FGameSaveSlotMetadata` 与 `FGameSaveSegmentPayload`

```cpp
USTRUCT(BlueprintType)
struct GAMEDATACORE_API FGameSaveSlotMetadata {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|Save")
    FString SlotName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|Save")
    FDateTime SaveTimestamp;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|Save")
    int32 ArchiveVersion = 1;
};

USTRUCT(BlueprintType)
struct GAMEDATACORE_API FGameSaveSegmentPayload {
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameData|Save")
    int32 SegmentVersion = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameData|Save")
    TArray<uint8> Bytes;
};
```

### 1.2 `FGameSaveSegment`

```cpp
struct GAMEDATACORE_API FGameSaveSegment {
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
    GENERATED_BODY()

public:
    static constexpr int32 CurrentArchiveVersion = 1;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "GameData|Save")
    FGameSaveSlotMetadata Metadata;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameData|Save")
    TMap<FName, FGameSaveSegmentPayload> Segments;
};
```

---

## 2. 存档核心类接口

### 2.1 门面调度器 `USaveGameSubsystem`

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

### 2.2 底层文件 I/O 与防断电保护 `FSaveGameFileIO`

```cpp
class GAMEDATACORE_API FSaveGameFileIO {
public:
    static FString GetSaveSlotFilePath(const FString& SlotName);
    static FString GetSaveSlotTempFilePath(const FString& SlotName);

    static bool IsSlotNameAllowed(const FString& SlotName, const TSet<FString>& AllowedSlotNames);
    static bool DoesSaveSlotExist(const FString& SlotName);
    static bool DeleteSaveSlot(const FString& SlotName);

    static bool SerializeObject(USaveGame* SaveGameObject, TArray<uint8>& OutBytes);

    static uint64 IssueSlotSaveGeneration(const FString& SlotName);
    static void InvalidateSlotSaveGeneration(const FString& SlotName);

    static bool WriteSlotBytes(const FString& SlotName, const TArray<uint8>& Bytes, uint64 SaveGeneration = 0);
    static bool ReadSlotBytes(const FString& SlotName, TArray<uint8>& OutBytes);
    static bool HasValidSaveGameHeader(const TArray<uint8>& Bytes, const FString& SlotName);
};
```

### 2.3 分段注册与版本平滑迁移 `FSaveGameSegmentRegistry`

```cpp
class GAMEDATACORE_API FSaveGameSegmentRegistry {
public:
    void RegisterSegment(const FGameSaveSegment& Segment);
    bool UnregisterSegment(FName SegmentName);
    [[nodiscard]] bool HasSegment(FName SegmentName) const;

    bool BuildArchive(const FString& SlotName, UGameSaveArchive& OutArchive) const;
    bool ApplyArchive(const FString& SlotName, const UGameSaveArchive& Archive);
    void ClearPreservedSegmentsForSlot(const FString& SlotName);
    void Reset();
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
| `LoadGameFromSlotAsync` | 后台读盘，回主线程反序列化并逐段应用；同槽位仅最新请求可应用，过期请求以 `OnLoadComplete(SlotName, false)` 取消 |
| `SaveObjectToSlot` | 任意 `USaveGame` 对象的存取逃生口，不走段注册 |
| `DoesSaveSlotExist` | 正式 `.sav` 或可抢救的 `.sav.tmp` 存在即返回 true |
| `DeleteSaveSlot` | 删除正式档与临时档并保留保存代次高水位，阻止在途异步写复活槽位；同时使在途异步加载失效。发现至少一个文件且全部删除成功才返回 `true` |

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
1. **自动临时档抢救**：若正式档 `.sav` 损坏、截断或缺失，自动回退读取未完成移动的 `.sav.tmp` 副本。临时副本格式合法且成功换入正式档后才返回成功；换入失败则本次读取返回 `false`；
2. **移动失败保留副本**：`WriteSlotBytes` 中若原子移动失败，不再直接删除临时文件，保留现场以便灾备抢救与排查；
3. **分段保护**：在分段归档时，若外部注册系统或对象指针失效，主动调用 `Ar.SetError()` 终止写入，杜绝向磁盘写入空白数据导致旧存档被覆盖清空；
4. **高水位代次防穿透与 ABA 免疫**：`InvalidateSlotSaveGeneration` 将槽位已提交代次推至当前签发水位（`CurrentIssued`），`WriteSlotBytes` 采用 `*Committed >= SaveGeneration` 进行绝对拦截。即使旧写抢在新写提交前到达，也因代次不满足条件而被安全吸收，杜绝槽位被意外复活；
5. **故障分支缓冲区安全重置**：`ReadSlotBytes` 在正式档损坏、临时档缺失、临时档损坏或原子重命名换入失败的所有异常分支，均显式调用 `OutBytes.Reset()`，杜绝截断或损坏数据在缓冲区残留。
