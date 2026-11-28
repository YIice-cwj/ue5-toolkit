# USaveGameSubsystem API 参考

> **一句话**：零业务字段的通用分段存档调度器，负责段注册、版本校验、原子落盘与灾备恢复。
> **头文件**：`Save/USaveGameSubsystem.h`、`Save/FSaveGameFileIO.h`、`Save/FSaveGameSegmentRegistry.h`、`Save/GameSaveArchive.h`、`Save/GameSaveTypes.h`、`Core/DataErrorCodes.h` · **线程**：段注册、同步存读与蓝图外观仅限 GameThread；异步存读在主线程捕获/序列化，后台读写文件，回 GameThread 应用与广播 · **失败语义**：`TGameStatusResult<T>` + `Game::ModuleId::Data` / `Game::DataErrorCodes`（`10020`–`10037`），蓝图外观返回 `bool`

## 30 秒速览

- 业务方注册 `FGameSaveSegment`（段名、段版本、`Save` / `Load` 委托），子系统不感知任何业务字段。
- 所有写入先落 `.sav.tmp`，再原子换入正式 `.sav`；正式档损坏或缺失时自动从临时档抢救。
- 槽位级代次单调递增；`DeleteSaveSlot` 在同一把槽位锁内推进高水位并删文件，阻止在途异步写复活槽位。
- 同步接口返回 `TGameStatusResult`；`SaveGameToSlotAsync` / `LoadGameFromSlotAsync` 返回 `bool`，并通过 `OnSaveComplete` / `OnLoadComplete` 广播。
- 云存档复用 `DeserializeAndValidateArchive` + `ApplyArchiveToSegments` 实现先验后落盘与零二次读盘。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `USaveGameSubsystem::RegisterSegment` | 注册业务存档段 | 段名非空、`SegmentVersion > 0`、读写委托有效，否则 `InvalidSegment` |
| `USaveGameSubsystem::UnregisterSegment` | 注销存档段 | 未注册段名返回 `SegmentNotFound` |
| `USaveGameSubsystem::SaveGameToSlot` | 同步保存到槽位 | 仅 GameThread；逐段捕获后原子写盘 |
| `USaveGameSubsystem::SaveGameToSlotAsync` | 异步保存到槽位 | 返回 `bool`；完成后广播 `OnSaveComplete` |
| `USaveGameSubsystem::LoadGameFromSlot` | 同步读取并逐段应用 | 损坏或版本不兼容返回具体错误码 |
| `USaveGameSubsystem::LoadGameFromSlotAsync` | 异步读取并逐段应用 | 同槽位仅最新请求可应用 |
| `USaveGameSubsystem::DeserializeAndValidateArchive` | 纯内存反序列化与版本预检 | 零磁盘 I/O、零业务状态修改 |
| `USaveGameSubsystem::ApplyArchiveToSegments` | 应用已校验内存归档 | 消除落盘后的二次读盘 |
| `USaveGameSubsystem::DeleteSaveSlot` | 删除槽位 | 同槽位锁内推进代次高水位并删除正式与临时文件 |
| `FSaveGameFileIO::WriteSlotBytes` | 原子写入槽位字节 | 先写 `.sav.tmp` 再换入；过期代次跳过写盘仍返回 `Success` |

## 核心类型

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

struct GAMEDATACORE_API FGameSaveSegment {
    FName SegmentName;
    int32 SegmentVersion = 1;
    TFunction<void(FArchive&)> Save;
    TFunction<bool(FArchive&, int32 StoredVersion)> Load;
};

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

- `FGameSaveSegment::SegmentVersion` 必须 `> 0`；`Load` 收到落盘时的 `StoredVersion`，供业务方自行迁移旧格式。
- 未注册的段在读取时原样保留、保存时写回，避免老版本客户端或升级场景丢段。
- `TFunction` 委托为空视为非法段，注册返回 `InvalidSegment`。

## 存档门面 `USaveGameSubsystem`

```cpp
void Initialize(FSubsystemCollectionBase& Collection) override;
void Deinitialize() override;

void SetSaveGameClass(TSubclassOf<UGameSaveArchive> InSaveGameClass);
TSubclassOf<UGameSaveArchive> GetSaveGameClass() const;

[[nodiscard]] TGameStatusResult<void> RegisterSegment(const FGameSaveSegment& Segment);
[[nodiscard]] TGameStatusResult<void> UnregisterSegment(FName SegmentName);
[[nodiscard]] bool HasSegment(FName SegmentName) const;

void SetAllowedSlotNames(const TArray<FString>& InAllowedSlotNames);
bool IsSlotNameAllowed(const FString& SlotName) const;

[[nodiscard]] TGameStatusResult<void> SaveGameToSlot(const FString& SlotName);
bool SaveGameToSlotAsync(const FString& SlotName);
[[nodiscard]] TGameStatusResult<void> LoadGameFromSlot(const FString& SlotName);
bool LoadGameFromSlotAsync(const FString& SlotName);

[[nodiscard]] TGameStatusResult<UGameSaveArchive*> DeserializeAndValidateArchive(const FString& SlotName, const TArray<uint8>& Bytes) const;
[[nodiscard]] TGameStatusResult<void> ApplyArchiveToSegments(const FString& SlotName, const UGameSaveArchive& Archive);

[[nodiscard]] TGameStatusResult<void> SaveObjectToSlot(const FString& SlotName, USaveGame* SaveGameObject);
[[nodiscard]] TGameStatusResult<USaveGame*> LoadObjectFromSlot(const FString& SlotName, TSubclassOf<USaveGame> SaveGameClassToLoad);
template <typename TSaveGameClass>
[[nodiscard]] TGameStatusResult<TSaveGameClass*> LoadObjectFromSlot(const FString& SlotName);

[[nodiscard]] bool DoesSaveSlotExist(const FString& SlotName) const;
[[nodiscard]] TGameStatusResult<void> DeleteSaveSlot(const FString& SlotName);

[[nodiscard]] static USaveGameSubsystem* GetPtr(const UObject* WorldContextObject);
[[nodiscard]] static USaveGameSubsystem& Get(const UObject* WorldContextObject);

UPROPERTY(BlueprintAssignable, Category = "GameData|Save")
FOnAsyncSaveComplete OnSaveComplete;

UPROPERTY(BlueprintAssignable, Category = "GameData|Save")
FOnAsyncLoadComplete OnLoadComplete;
```

### 接口契约

| 接口 | 参数 | 返回 | 语义 |
| :--- | :--- | :--- | :--- |
| `RegisterSegment` | `Segment` | `Success` / `Failure(InvalidSegment)` | 按段名覆盖注册；校验段名非空、`SegmentVersion > 0`、读写委托均有效 |
| `UnregisterSegment` | `SegmentName` | `Success` / `Failure(SegmentNotFound)` | 注销未注册段名返回 `SegmentNotFound` |
| `SaveGameToSlot` | `SlotName` | `TGameStatusResult<void>` | 主线程逐段捕获并原子写盘 |
| `SaveGameToSlotAsync` | `SlotName` | `bool` | 主线程捕获段字节，后台写盘；完成后广播 `OnSaveComplete` |
| `LoadGameFromSlot` | `SlotName` | `TGameStatusResult<void>` | 同步读取、校验并逐段应用；损坏或版本不兼容返回具体错误码 |
| `LoadGameFromSlotAsync` | `SlotName` | `bool` | 后台读盘，回主线程反序列化并逐段应用；同槽位仅最新请求可应用 |
| `DeserializeAndValidateArchive` | `SlotName`、`Bytes` | `TGameStatusResult<UGameSaveArchive*>` | 纯内存反序列化与版本预检，零磁盘 I/O、零业务状态修改 |
| `ApplyArchiveToSegments` | `SlotName`、`Archive` | `TGameStatusResult<void>` | 应用已校验内存归档，消除落盘后的二次读盘 |
| `SaveObjectToSlot` | `SlotName`、`SaveGameObject` | `TGameStatusResult<void>` | 任意 `USaveGame` 对象的存取逃生口，不走段注册 |
| `LoadObjectFromSlot` | `SlotName`、`SaveGameClassToLoad` | `TGameStatusResult<USaveGame*>` | 类型不匹配返回 `SaveTypeMismatch` |
| `DoesSaveSlotExist` | `SlotName` | `bool` | 正式 `.sav` 或任意 `.sav.tmp` 物理文件存在即 `true`，不校验内容 |
| `DeleteSaveSlot` | `SlotName` | `TGameStatusResult<void>` | 同锁内推进代次高水位并删除正式与临时文件 |

- **失败语义**：`SaveGameToSlotAsync` / `LoadGameFromSlotAsync` 的前置校验失败返回 `false`；异步加载的过期请求以 `OnLoadComplete(SlotName, false)` 取消。
- **生命周期**：`Deinitialize` 经 `FCancellationTokenSource` 取消在途异步回调，并清空段注册表、白名单与异步加载代次。
- **归档类型**：`SetSaveGameClass` 拒绝空类与含 `CLASS_Abstract` 的类，拒绝时保持原值。

### 蓝图外观

```cpp
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
```

- 蓝图外观内部转发到对应的 `TGameStatusResult` 接口并返回 `bool`，错误码需通过日志或 C++ 接口获取。
- `K2_LoadObjectFromSlot` 失败时把 `OutSaveGame` 置为 `nullptr`。

### 最小示例

```cpp
USaveGameSubsystem* SaveSys = USaveGameSubsystem::GetPtr(this);

FGameSaveSegment PlayerSegment;
PlayerSegment.SegmentName = TEXT("Player");
PlayerSegment.SegmentVersion = 1;
PlayerSegment.Save = [this](FArchive& Ar) { Ar << PlayerLevel; };
PlayerSegment.Load = [this](FArchive& Ar, int32 StoredVersion) {
    Ar << PlayerLevel;
    return !Ar.IsError();
};
SaveSys->RegisterSegment(PlayerSegment);

const TGameStatusResult<void> SaveResult = SaveSys->SaveGameToSlot(TEXT("Slot_01"));
```

## 底层文件 I/O `FSaveGameFileIO`

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
    [[nodiscard]] static bool IsSlotSaveGenerationSuperseded(const FString& SlotName, uint64 SaveGeneration);

    [[nodiscard]] static TGameStatusResult<void> WriteSlotBytes(
        const FString& SlotName,
        const TArray<uint8>& Bytes,
        uint64 SaveGeneration = 0,
        bool* bOutSkippedStale = nullptr
    );
    [[nodiscard]] static TGameStatusResult<TArray<uint8>> ReadSlotBytes(const FString& SlotName);
    [[nodiscard]] static bool HasValidSaveGameHeader(const TArray<uint8>& Bytes, const FString& SlotName);
};
```

| 方法 | 说明 |
| :--- | :--- |
| `IssueSlotSaveGeneration` | 只锁内存代次表签发递增序号，不阻塞于后台磁盘 I/O；删除后仍沿高水位递增，不复用历史代次 |
| `InvalidateSlotSaveGeneration` | 单调推进签发与已提交高水位，使在途保存代次全部失效 |
| `IsSlotSaveGenerationSuperseded` | 判断写入代次是否已被更新的签发或删槽水位超越 |
| `WriteSlotBytes` | 先写 `.sav.tmp` 再原子换入；`SaveGeneration != 0` 时校验高水位，过期写安全跳过并返回 `Success` |
| `ReadSlotBytes` | 正式档损坏或缺失时自动自 `.sav.tmp` 抢救并原子换入；临时档也损坏返回 `CorruptedSaveFile` |
| `HasValidSaveGameHeader` | 校验前导魔数 `0x53415647` 与最小头长度（`>= 64` 字节） |

## 分段注册表 `FSaveGameSegmentRegistry`

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

- `ValidateArchiveCompatibility` 只读无副作用，供落盘前预检；任一段版本高于当前注册版本返回 `SegmentVersionMismatch`。
- `ApplyArchive` 按段版本分发；未知段自动保留到 `PreservedSegmentsBySlot`，后续保存原样写回。
- `BuildArchive` 驱动全部已注册段序列化，任一段 `Save` 置位 `Ar.IsError()` 返回 `SegmentSaveFailed`。

## 槽位校验与并发 / 灾备

`IsSlotNameAllowed` 规则：

1. 非空且长度不超过 64；
2. 不含首尾空白、控制字符（`< 0x20` 或 `0x7F`）及 `/`、`\`、`.`、`:`、`*`、`?`、`"`、`<`、`>`、`|`；
3. 白名单非空时必须命中白名单。

不合法槽位在同步保存/加载/删除入口返回 `InvalidSlotName`；`DoesSaveSlotExist` 返回 `false`；异步接口记录 Warning 并立即广播 `false`。

- **双层锁**：轻量内存元数据锁保护代次表与槽位锁映射，按槽位隔离的 I/O 互斥锁保护磁盘读写；不同槽位可并行读写。
- **临时档抢救**：仅在 `.sav.tmp` 格式合法且成功原子换入正式档后返回 `Success`；换入失败返回 `RecoveryMoveFailed`。
- **写失败清理 / 换入失败保留**：写 `.tmp` 阶段失败自动清理残缺临时文件；`.tmp` 写完整但原子 `Move` 失败时保留完整 `.tmp` 供后续抢救。
- **删除全成功语义**：正式档与临时档全部删除成功才返回 `Success`；任一删除失败返回 `DeleteSlotFailed`；两者都不存在返回 `SaveSlotNotFound`。

## 错误码 `Game::DataErrorCodes`

<details>
<summary><b>展开完整错误码表（18 项）</b></summary>

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

</details>

## 常见错误与边界

- **槽位非法**：同步入口返回 `InvalidSlotName`，`DoesSaveSlotExist` 返回 `false`，异步入口广播 `false` 且不派发任务。
- **段版本不兼容**：落盘段版本高于注册版本时整档拒绝，不做隐式降级；低于当前版本由业务方在 `Load` 中迁移。
- **异步加载竞态**：同一槽位仅最新代次可应用，过期请求以 `OnLoadComplete(SlotName, false)` 取消，不会覆盖新状态。
- **删除语义**：只有正式档与临时档全部删除成功才算成功；删除后该槽位在途异步加载失效。
- **蓝图输出映射**：`K2_LoadObjectFromSlot` 失败时把 `OutSaveGame` 置为 `nullptr`，调用方不会拿到上一轮的残留指针。
- **临时档恢复**：正式档损坏或缺失时自动读取 `.sav.tmp`；临时档也损坏返回 `CorruptedSaveFile`，恢复输出字节被清空。

## 相关文档

- [插件 README](../../README.md)
- [持久化架构](../Architecture/PersistenceArchitecture.md)
- [存档接入指南](../Guides/SaveGameGuide.md)
- [云存档 API](GameCloudSaveSubsystem.md)
