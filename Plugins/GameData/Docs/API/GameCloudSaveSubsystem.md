# UGameCloudSaveSubsystem API 参考

> **一句话**：基于 `UGameNetServiceSubsystem` 强类型 RPC 的云端存档上传、下载与元数据查询门面。
> **头文件**：`Cloud/GameCloudSaveSubsystem.h`、`Cloud/GameCloudSaveTypes.h` · **线程**：入口在 GameThread 调用；本地读档与落盘在后台执行，完成回调与蓝图委托在 GameThread 结算 · **失败语义**：`EGameCloudSaveResult` + 响应结构体的 `ErrorCode` / `ErrorMessage`，C++ 回调与蓝图委托双通道

## 30 秒速览

- 三条通道：上传、下载、元数据查询，固定占用 MessageId `201`–`206`。
- 下载执行「先内存校验、后原子落盘」：魔数头、反序列化、归档与分段版本预检全部通过才写 `.sav`，本地健康存档零污染。
- `bAutoApplyToSubsystem == true` 时，已校验的内存 `UGameSaveArchive` 直接热应用至已注册段，零二次读盘。
- 所有异步入口接受 `FCancellationToken`；`Deinitialize` 取消在途任务并清理待热应用归档。
- 蓝图入口 `UploadSlot` / `DownloadSlot` 广播 `OnCloudUploadComplete` / `OnCloudDownloadComplete`。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `UGameCloudSaveSubsystem::GetPtr` | 取云存档子系统指针 | 未就绪返回 `nullptr` |
| `UGameCloudSaveSubsystem::UploadSlotAsync` | 异步上传槽位到云端 | GameThread 调用；回调在 GameThread；可传取消令牌 |
| `UGameCloudSaveSubsystem::UploadSlot` | 蓝图上传外观 | 默认超时 `10.0f`；完成后广播 `OnCloudUploadComplete` |
| `UGameCloudSaveSubsystem::DownloadSlotAsync` | 异步下载并按需热应用 | 校验失败在落盘前拦截；可传取消令牌 |
| `UGameCloudSaveSubsystem::DownloadSlot` | 蓝图下载外观 | `bAutoApplyToSubsystem` 默认 `true` |
| `UGameCloudSaveSubsystem::QueryCloudSlotMetadataAsync` | 查询云端槽位元数据 | 404 或不存在映射为 `SlotNotFound` |
| `UGameCloudSaveSubsystem::Deinitialize` | 取消在途请求并清理待热应用归档 | 子系统销毁时调用；销毁后回调不再保证投递 |

## 结果枚举 `EGameCloudSaveResult`

```cpp
UENUM(BlueprintType)
enum class EGameCloudSaveResult : uint8 {
    Success,
    NetworkError,
    SlotNotFound,
    CorruptedData,
    VersionMismatch,
    Cancelled,
    IoError
};
```

| 枚举值 | 触发条件 |
| :--- | :--- |
| `Success` | 上传完成；下载校验通过并落盘（可选热应用）完成；查询完成 |
| `NetworkError` | 网络未就绪、断线、服务端内部错误（如 500）或 RPC 超时 |
| `SlotNotFound` | 槽位名不合法、本地无此槽位，或云端不存在（404） |
| `CorruptedData` | 魔数头、内存反序列化或数据完整性校验不通过（落盘前拦截） |
| `VersionMismatch` | 服务端版本冲突（409），或归档/分段版本高于当前客户端支持版本（落盘前拦截） |
| `Cancelled` | 调用方 `FCancellationToken` 或子系统 `Deinitialize` 主动取消 |
| `IoError` | 本地磁盘原子写入失败 |

## 请求与响应结构体

全部为 `USTRUCT(BlueprintType)` 且带 `GAMEDATA_API`。

| 结构体 | MessageId | 字段 |
| :--- | :---: | :--- |
| `FGameCloudSaveUploadReq` | `201` | `SlotName`、`ArchiveVersion`、`SaveTimestamp`、`SaveBytes` |
| `FGameCloudSaveUploadRsp` | `202` | `bSuccess`、`SlotName`、`ErrorCode`、`ErrorMessage`、`CloudTimestamp` |
| `FGameCloudSaveDownloadReq` | `203` | `SlotName` |
| `FGameCloudSaveDownloadRsp` | `204` | `bSuccess`、`SlotName`、`ErrorCode`、`ErrorMessage`、`ArchiveVersion`、`CloudTimestamp`、`SaveBytes` |
| `FGameCloudSaveQueryReq` | `205` | `SlotName` |
| `FGameCloudSaveQueryRsp` | `206` | `bSuccess`、`SlotName`、`bExistsOnCloud`、`ErrorCode`、`ErrorMessage`、`ArchiveVersion`、`CloudTimestamp`、`SaveSizeBytes` |

## 生命周期与寻址

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnCloudSaveUploadDelegate,
    const FString&, SlotName,
    EGameCloudSaveResult, Result,
    const FString&, ErrorMessage
);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnCloudSaveDownloadDelegate,
    const FString&, SlotName,
    EGameCloudSaveResult, Result,
    const FString&, ErrorMessage
);

using FOnCloudUploadCompleted = TFunction<void(EGameCloudSaveResult, const FGameCloudSaveUploadRsp&)>;
using FOnCloudDownloadCompleted = TFunction<void(EGameCloudSaveResult, const FGameCloudSaveDownloadRsp&)>;
using FOnCloudQueryCompleted = TFunction<void(EGameCloudSaveResult, const FGameCloudSaveQueryRsp&)>;

void Initialize(FSubsystemCollectionBase& Collection) override;
void Deinitialize() override;

[[nodiscard]] static UGameCloudSaveSubsystem* GetPtr(const UObject* WorldContextObject);
[[nodiscard]] static UGameCloudSaveSubsystem& Get(const UObject* WorldContextObject);

UPROPERTY(BlueprintAssignable, Category = "GameData|CloudSave")
FOnCloudSaveUploadDelegate OnCloudUploadComplete;

UPROPERTY(BlueprintAssignable, Category = "GameData|CloudSave")
FOnCloudSaveDownloadDelegate OnCloudDownloadComplete;
```

- `GetPtr` 支持 `UWorld`、`AActor`、`UGameInstance`、`UGameInstanceSubsystem` 上下文；未就绪返回 `nullptr`。
- `Deinitialize` 取消全部在途请求，并清空 `PendingDownloadArchives`。
- 蓝图层使用 `OnCloudUploadComplete` / `OnCloudDownloadComplete`（`DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams`，参数为 `SlotName`、`Result`、`ErrorMessage`）。

## 上传

```cpp
void UploadSlotAsync(
    const FString& SlotName,
    float TimeoutSeconds,
    FOnCloudUploadCompleted Callback,
    FCancellationToken CancellationToken = FCancellationToken::None()
);

UFUNCTION(BlueprintCallable, Category = "GameData|CloudSave")
void UploadSlot(const FString& SlotName, float TimeoutSeconds = 10.0f);
```

| 参数 | 类型 | 说明 |
| :--- | :--- | :--- |
| `SlotName` | `const FString&` | 本地与云端共用的槽位名，需通过本地白名单校验 |
| `TimeoutSeconds` | `float` | RPC 超时秒数；蓝图入口默认 `10.0f` |
| `Callback` | `FOnCloudUploadCompleted` | 完成闭包，成功与失败都会调用 |
| `CancellationToken` | `FCancellationToken` | 可选协同取消令牌，默认 `None()` |

- **流程**：校验取消令牌 → 槽位合法性 → 网络连接；后台经 `FSaveGameFileIO::ReadSlotBytes` 读本地快照并校验头结构；回主线程封装 `FGameCloudSaveUploadReq` 并经 `SendStructRequest` 发送；响应后结算并广播。
- **映射**：`409` → `VersionMismatch`，`404` → `SlotNotFound`，其余服务端失败 → `NetworkError`。
- **失败前置**：槽位不合法直接 `SlotNotFound`，网络未连接直接 `NetworkError`，均不发起 RPC。

## 下载

```cpp
void DownloadSlotAsync(
    const FString& SlotName,
    bool bAutoApplyToSubsystem,
    float TimeoutSeconds,
    FOnCloudDownloadCompleted Callback,
    FCancellationToken CancellationToken = FCancellationToken::None()
);

UFUNCTION(BlueprintCallable, Category = "GameData|CloudSave")
void DownloadSlot(const FString& SlotName, bool bAutoApplyToSubsystem = true, float TimeoutSeconds = 10.0f);
```

| 参数 | 类型 | 说明 |
| :--- | :--- | :--- |
| `SlotName` | `const FString&` | 目标槽位名 |
| `bAutoApplyToSubsystem` | `bool` | 落盘成功后是否把已校验归档热应用至当前已注册段；蓝图入口默认 `true` |
| `TimeoutSeconds` | `float` | RPC 超时秒数；蓝图入口默认 `10.0f` |
| `Callback` | `FOnCloudDownloadCompleted` | 完成闭包 |
| `CancellationToken` | `FCancellationToken` | 可选协同取消令牌 |

- **先验后落盘**：收到云端字节后，在触碰本地磁盘前执行 `ValidateDownloadedBytes`（魔数头 + `DeserializeAndValidateArchive` 内存反序列化 + 归档/分段版本预检）。失败直接返回 `CorruptedData` 或 `VersionMismatch`。
- **落盘**：请求派发时签发单调代次 `IssueSlotSaveGeneration`，响应通过校验后以该代次后台原子写入 `.sav`；失败返回 `IoError`。
- **热应用**：`bAutoApplyToSubsystem == true` 时，把同一份已校验内存归档交给 `ApplyArchiveToSegments`，不二次读盘、不二次反序列化。
- **过期写防护**：响应到达先判 `IsSlotSaveGenerationSuperseded`；请求在途期间发生本地保存、删槽或同槽位更新下载时，旧响应返回 `Cancelled` 且不触碰本地档。

### 最小示例

```cpp
UGameCloudSaveSubsystem* Cloud = UGameCloudSaveSubsystem::GetPtr(this);
Cloud->DownloadSlotAsync(
    TEXT("Slot_01"),
    true,
    10.0f,
    [](EGameCloudSaveResult Result, const FGameCloudSaveDownloadRsp& Rsp) {
        if (Result == EGameCloudSaveResult::Success) {
            GAME_LOG(Log, TEXT("云存档下载完成: %s"), *Rsp.SlotName);
        }
    }
);
```

## 元数据查询

```cpp
void QueryCloudSlotMetadataAsync(
    const FString& SlotName,
    float TimeoutSeconds,
    FOnCloudQueryCompleted Callback,
    FCancellationToken CancellationToken = FCancellationToken::None()
);
```

| 参数 | 类型 | 说明 |
| :--- | :--- | :--- |
| `SlotName` | `const FString&` | 待查询槽位名 |
| `TimeoutSeconds` | `float` | RPC 超时秒数 |
| `Callback` | `FOnCloudQueryCompleted` | 查询完成闭包 |
| `CancellationToken` | `FCancellationToken` | 可选协同取消令牌 |

- 用于下载前比对本地与云端的 `CloudTimestamp`、`SaveSizeBytes`。
- `!bSuccess && ErrorCode == 404` 或 `bSuccess && !bExistsOnCloud` 映射为 `SlotNotFound`；其余服务端失败映射为 `NetworkError`；`ErrorCode` 与 `ErrorMessage` 原样透传。

## 常见错误与边界

- **槽位不合法**：直接返回 `SlotNotFound` 并广播委托，不发起网络请求；槽位规则与 `USaveGameSubsystem::IsSlotNameAllowed` 一致。
- **下载校验失败**：返回 `CorruptedData` / `VersionMismatch`，本地既有健康 `.sav` 不被改写。
- **落盘失败**：返回 `IoError`；已通过校验的归档不会热应用。
- **取消**：令牌触发或子系统销毁返回 `Cancelled`；销毁后的回调不保证再投递。
- **回调线程**：C++ 回调与蓝图委托均在 GameThread 结算，可直接操作 UObject。

## 相关文档

- [插件 README](../../README.md)
- [持久化架构](../Architecture/PersistenceArchitecture.md)
- [存档接入指南](../Guides/SaveGameGuide.md)
- [存档子系统 API](SaveGameSubsystem.md)
