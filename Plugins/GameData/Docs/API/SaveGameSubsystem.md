# GameData 存档子系统 API 参考

`USaveGameSubsystem`（`GameDataCore`）是零业务字段的通用分段存档调度器。所有错误均统一归属 `Game::ModuleId::Data` 并携带强类型 `Game::DataErrorCodes` 错误码（定义于 `Core/DataErrorCodes.h`）。

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
    int32 SegmentVersion = 1; // 必须 > 0
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
    GENERATED_BODY()

public:
    void Initialize(FSubsystemCollectionBase& Collection) override;
    void Deinitialize() override;

    void SetSaveGameClass(TSubclassOf<UGameSaveArchive> InSaveGameClass); // 拒绝空类及 CLASS_Abstract
    TSubclassOf<UGameSaveArchive> GetSaveGameClass() const;

    [[nodiscard]] TGameStatusResult<void> RegisterSegment(const FGameSaveSegment& Segment);
    [[nodiscard]] TGameStatusResult<void> UnregisterSegment(FName SegmentName);
    [[nodiscard]] bool HasSegment(FName SegmentName) const;

    void SetAllowedSlotNames(const TArray<FString>& InAllowedSlotNames);
    bool IsSlotNameAllowed(const FString& SlotName) const;

    // C++ 强契约主接口（统一返回 TGameStatusResult，携带 Game::ModuleId::Data 与 Game::DataErrorCodes）
    [[nodiscard]] TGameStatusResult<void> SaveGameToSlot(const FString& SlotName);
    [[nodiscard]] TGameStatusResult<void> LoadGameFromSlot(const FString& SlotName);
    [[nodiscard]] TGameStatusResult<UGameSaveArchive*> DeserializeAndValidateArchive(const FString& SlotName, const TArray<uint8>& Bytes) const;
    [[nodiscard]] TGameStatusResult<void> ApplyArchiveToSegments(const FString& SlotName, const UGameSaveArchive& Archive);
    [[nodiscard]] TGameStatusResult<void> SaveObjectToSlot(const FString& SlotName, USaveGame* SaveGameObject);
    [[nodiscard]] TGameStatusResult<USaveGame*> LoadObjectFromSlot(const FString& SlotName, TSubclassOf<USaveGame> SaveGameClassToLoad);
    template <typename TSaveGameClass>
    [[nodiscard]] TGameStatusResult<TSaveGameClass*> LoadObjectFromSlot(const FString& SlotName);
    [[nodiscard]] TGameStatusResult<void> DeleteSaveSlot(const FString& SlotName);

    // 蓝图反射外观方法（内部转发并返回 bool）
    UFUNCTION(BlueprintCallable, Category = "GameData|Save", meta = (DisplayName = "Save Game To Slot"))
    bool K2_SaveGameToSlot(const FString& SlotName);

    UFUNCTION(BlueprintCallable, Category = "GameData|Save", meta = (DisplayName = "Load Game From Slot"))
    bool K2_LoadGameFromSlot(const FString& SlotName);

    UFUNCTION(BlueprintCallable, Category = "GameData|Save", meta = (DisplayName = "Save Object To Slot"))
    bool K2_SaveObjectToSlot(const FString& SlotName, USaveGame* SaveGameObject);

    UFUNCTION(BlueprintCallable, Category = "GameData|Save", meta = (DisplayName = "Load Object From Slot"))
    bool K2_LoadObjectFromSlot(const FString& SlotName, TSubclassOf<USaveGame> SaveGameClassToLoad, USaveGame*& OutSaveGame);

    UFUNCTION(BlueprintCallable, Category = "GameData|Save", meta = (DisplayName = "Delete Save Slot"))
    bool K2_DeleteSaveSlot(const FString& SlotName);

    // 异步分段任务接口（Deinitialize 自动经 FCancellationTokenSource 取消在途回调）
    bool SaveGameToSlotAsync(const FString& SlotName);
    bool LoadGameFromSlotAsync(const FString& SlotName);
    bool DoesSaveSlotExist(const FString& SlotName) const;

    [[nodiscard]] static USaveGameSubsystem* GetPtr(const UObject* WorldContextObject);
    [[nodiscard]] static USaveGameSubsystem& Get(const UObject* WorldContextObject);

    FOnAsyncSaveComplete OnSaveComplete;
    FOnAsyncLoadComplete OnLoadComplete;
};
```

### 2.2 底层文件 I/O 与防断电保护 `FSaveGameFileIO`

```cpp
class GAMEDATACORE_API FSaveGameFileIO {
public:
    FSaveGameFileIO() = delete;

    [[nodiscard]] static FString GetSaveSlotFilePath(const FString& SlotName);
    [[nodiscard]] static FString GetSaveSlotTempFilePath(const FString& SlotName);

    [[nodiscard]] static bool IsSlotNameAllowed(const FString& SlotName, const TSet<FString>& AllowedSlotNames);
    [[nodiscard]] static bool DoesSaveSlotExist(const FString& SlotName);
    [[nodiscard]] static TGameStatusResult<void> DeleteSaveSlot(const FString& SlotName);

    [[nodiscard]] static TGameStatusResult<TArray<uint8>> SerializeObject(USaveGame* SaveGameObject);

    [[nodiscard]] static uint64 IssueSlotSaveGeneration(const FString& SlotName);
    static void InvalidateSlotSaveGeneration(const FString& SlotName);

    [[nodiscard]] static TGameStatusResult<void> WriteSlotBytes(const FString& SlotName, const TArray<uint8>& Bytes, uint64 SaveGeneration = 0, bool* bOutSkippedStale = nullptr);
    [[nodiscard]] static TGameStatusResult<TArray<uint8>> ReadSlotBytes(const FString& SlotName);
    [[nodiscard]] static bool HasValidSaveGameHeader(const TArray<uint8>& Bytes, const FString& SlotName);
};
```

### 2.3 分段注册与版本平滑迁移 `FSaveGameSegmentRegistry`

```cpp
class GAMEDATACORE_API FSaveGameSegmentRegistry {
public:
    [[nodiscard]] TGameStatusResult<void> RegisterSegment(const FGameSaveSegment& Segment);
    [[nodiscard]] TGameStatusResult<void> UnregisterSegment(FName SegmentName);
    [[nodiscard]] bool HasSegment(FName SegmentName) const;

    [[nodiscard]] TGameStatusResult<void> ValidateArchiveCompatibility(const UGameSaveArchive& Archive) const;
    [[nodiscard]] TGameStatusResult<void> BuildArchive(const FString& SlotName, UGameSaveArchive& OutArchive) const;
    [[nodiscard]] TGameStatusResult<void> ApplyArchive(const FString& SlotName, const UGameSaveArchive& Archive);
    void ClearPreservedSegmentsForSlot(const FString& SlotName);
    void Reset();
};
```

---

## 3. 语义说明

| 接口 | 语义 |
| :--- | :--- |
| `RegisterSegment` | 按段名覆盖注册；校验段名非空、`SegmentVersion > 0` 及保存/加载委托均有效，不满足返回 `InvalidSegment` |
| `SaveGameToSlot` | 主线程逐段捕获并原子写盘；返回 `TGameStatusResult<void>` |
| `SaveGameToSlotAsync` | 主线程捕获段字节，后台写盘；完成后广播 `OnSaveComplete`，`Deinitialize` 取消在途回调 |
| `LoadGameFromSlot` | 同步读取、校验并逐段应用；损坏或版本不兼容返回带具体错误码的 `Failure` |
| `DeserializeAndValidateArchive` | 纯内存反序列化与归档/段版本预检（零磁盘 I/O 与零状态变更），供云存档先验后落盘复用 |
| `ApplyArchiveToSegments` | 直接应用已校验的内存归档，消除落盘后的二次读盘 |
| `LoadGameFromSlotAsync` | 后台读盘，回主线程反序列化并逐段应用；同槽位仅最新请求可应用，过期请求以 `OnLoadComplete(SlotName, false)` 取消 |
| `SaveObjectToSlot` | 任意 `USaveGame` 对象的存取逃生口，不走段注册 |
| `DoesSaveSlotExist` | 正式 `.sav` 或可抢救的 `.sav.tmp` 存在即返回 `true` |
| `DeleteSaveSlot` | 在同一把槽位 I/O 锁内原子完成代次高水位推进与文件删除，阻止在途异步写复活槽位 |

`OnSaveComplete` / `OnLoadComplete` 仅异步路径广播；同步路径由 `TGameStatusResult` 返回值判定。

---

## 4. 槽位校验

`IsSlotNameAllowed` 规则：

1. 非空且长度不超过 64；
2. 不含首尾空白、控制字符（`< 0x20` 或 `0x7F`）及 `/`、`\`、`.`、`:`、`*`、`?`、`"`、`<`、`>`、`|`；
3. 白名单非空时必须命中白名单。

不合法槽位在同步保存/加载/删除入口直接返回 `InvalidSlotName`（`DoesSaveSlotExist` 返回 `false`；异步接口记录 Warning 并立即广播 `false`）。

---

## 5. 并发锁分层与灾备恢复机制

1. **双层锁隔离（零主线程 I/O 阻塞）**：`FSaveGameFileIO` 将轻量的内存元数据锁（保护代次表与槽位锁映射）与**按槽位隔离的 I/O 互斥锁**分离；`IssueSlotSaveGeneration` 在主线程纯内存纳秒级完成，绝不被其他槽位磁盘写阻塞，且多槽位可并行读写；
2. **自动临时档抢救**：若正式档 `.sav` 损坏、截断或缺失，自动回退读取 `.sav.tmp` 副本；仅在临时副本格式合法且成功原子换入正式档后返回 `Success`；
3. **写失败清理 / 换入失败保留**：`WriteSlotBytes` 借助 `TCancelableScopeGuard`，若写入 `.tmp` 阶段失败自动清理残缺 `.tmp`；若 `.tmp` 写完整但原子 `Move` 失败，则保留完整 `.tmp` 供后续自动抢救恢复；
4. **高水位代次防穿透与 ABA 免疫**：`DeleteSaveSlot` 持槽位锁同步推进签发与已提交水位并删盘，`WriteSlotBytes` 采用 `*Committed >= SaveGeneration` 拦截一切过期写，且 `IsSlotSaveGenerationSuperseded` 可检测后台落盘至主线程热应用窗口期内的新写/删槽超越，彻底消除旧归档复活或倒灌的竞态窗口。

---

## 6. 持久化错误码速查表 (`Game::DataErrorCodes`)

| 错误码常量 | 数值 | 触发场景 |
| :--- | :---: | :--- |
| `InvalidSlotName` | `10020` | 槽位名为空、超长（>64）、含非法字符或不在白名单内 |
| `InvalidSaveObject` | `10021` | 待序列化的 `USaveGame*` 对象为空 |
| `InvalidSaveClass` | `10022` | 目标存档 `UClass` 为空或包含 `CLASS_Abstract` 抽象标志 |
| `SaveSerializeFailed` | `10023` | `UGameplayStatics::SaveGameToMemory` 序列化失败 |
| `SaveDeserializeFailed` | `10024` | `UGameplayStatics::LoadGameFromMemory` 反序列化失败 |
| `SaveTypeMismatch` | `10025` | 反序列化出的对象类型与期望的 `SaveGameClass` 不匹配 |
| `ArchiveVersionMismatch` | `10026` | 归档主版本 `ArchiveVersion <= 0` 或高于 `CurrentArchiveVersion` |
| `SaveSlotNotFound` | `10027` | 指定槽位的 `.sav` 与 `.sav.tmp` 均不存在 |
| `DeleteSlotFailed` | `10028` | 槽位物理文件存在但文件系统删除失败 |
| `WriteTempFileFailed` | `10029` | 写入 `.sav.tmp` 临时存档文件失败 |
| `CommitTempFileFailed` | `10030` | 将 `.sav.tmp` 原子替换为正式 `.sav` 失败 |
| `CorruptedSaveFile` | `10031` | 正式档与临时档魔数头（`0x53415647`）或长度校验均不通过 |
| `RecoveryMoveFailed` | `10032` | 自合法 `.sav.tmp` 抢救恢复并原子换入正式 `.sav` 失败 |
| `InvalidSegment` | `10033` | 待注册存档段名为空、`SegmentVersion <= 0` 或读写委托缺失 |
| `SegmentNotFound` | `10034` | 注销未注册的存档段名 |
| `SegmentSaveFailed` | `10035` | 存档段 `Save(FArchive&)` 置位 `Ar.IsError()` |
| `SegmentVersionMismatch` | `10036` | 存档段存储版本 `<= 0` 或高于当前注册的 `SegmentVersion` |
| `SegmentLoadFailed` | `10037` | 存档段 `Load` 返回 `false`、`Ar.IsError()` 或触发 `ArMaxSerializeSize` 越界保护 |
