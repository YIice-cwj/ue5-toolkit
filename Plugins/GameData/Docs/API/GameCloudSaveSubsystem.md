# GameCloudSaveSubsystem API 参考

`UGameCloudSaveSubsystem` 是 `GameData` 模块对业务层暴露的云端存档同步门面。它基于 `UGameNetServiceSubsystem` 的强类型 RPC 双向请求通道，结合底层 `FSaveGameFileIO` 与 `USaveGameSubsystem`，提供**先内存校验后原子落盘**的本地零破坏保护、协同取消令牌（`FCancellationToken`）与全链路 `GameLogging` 观测。

---

## 包含头文件

```cpp
#include "Cloud/GameCloudSaveSubsystem.h"
#include "Cloud/GameCloudSaveTypes.h"
```

---

## 结果枚举：`EGameCloudSaveResult`

```cpp
UENUM(BlueprintType)
enum class EGameCloudSaveResult : uint8 {
    Success,         // 操作成功（上传完成 / 下载校验落盘并可选应用完成 / 查询完成）
    NetworkError,    // 网络不可用、断线、服务端内部错误（如 500）或 RPC 超时
    SlotNotFound,    // 槽位名不合法、本地无此槽位或云端不存在（404）
    CorruptedData,   // 头部魔数、反序列化或数据完整性校验不通过（落盘前拦截）
    VersionMismatch, // 服务端版本冲突（409）或归档/分段版本高于当前客户端支持版本（落盘前拦截）
    Cancelled,       // 被调用方 FCancellationToken 或子系统 Deinitialize 主动取消
    IoError          // 本地磁盘原子写入失败
};
```

---

## 请求与响应结构体

### 上传协议
- **`FGameCloudSaveUploadReq`** (MessageId: 201)：`SlotName`, `ArchiveVersion`, `SaveTimestamp`, `SaveBytes`
- **`FGameCloudSaveUploadRsp`** (MessageId: 202)：`bSuccess`, `SlotName`, `ErrorCode`, `ErrorMessage`, `CloudTimestamp`

### 下载协议
- **`FGameCloudSaveDownloadReq`** (MessageId: 203)：`SlotName`
- **`FGameCloudSaveDownloadRsp`** (MessageId: 204)：`bSuccess`, `SlotName`, `ErrorCode`, `ErrorMessage`, `ArchiveVersion`, `CloudTimestamp`, `SaveBytes`

### 元数据查询协议
- **`FGameCloudSaveQueryReq`** (MessageId: 205)：`SlotName`
- **`FGameCloudSaveQueryRsp`** (MessageId: 206)：`bSuccess`, `SlotName`, `bExistsOnCloud`, `ErrorCode`, `ErrorMessage`, `ArchiveVersion`, `CloudTimestamp`, `SaveSizeBytes`

---

## 类声明核心接口

### 0. 回调类型别名、多播委托与生命周期

```cpp
using FOnCloudUploadCompleted = TFunction<void(EGameCloudSaveResult, const FGameCloudSaveUploadRsp&)>;
using FOnCloudDownloadCompleted = TFunction<void(EGameCloudSaveResult, const FGameCloudSaveDownloadRsp&)>;
using FOnCloudQueryCompleted = TFunction<void(EGameCloudSaveResult, const FGameCloudSaveQueryRsp&)>;

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

void Initialize(FSubsystemCollectionBase& Collection) override;
void Deinitialize() override;

UPROPERTY(BlueprintAssignable, Category = "GameData|CloudSave")
FOnCloudSaveUploadDelegate OnCloudUploadComplete;

UPROPERTY(BlueprintAssignable, Category = "GameData|CloudSave")
FOnCloudSaveDownloadDelegate OnCloudDownloadComplete;
```

### 1. 静态寻址

```cpp
[[nodiscard]] static UGameCloudSaveSubsystem* GetPtr(const UObject* WorldContextObject);
[[nodiscard]] static UGameCloudSaveSubsystem& Get(const UObject* WorldContextObject);
```

支持从 `UWorld`、`AActor`、`UActorComponent`、`UGameInstance` 以及同级 `UGameInstanceSubsystem` 寻址。

### 2. 异步上传本地存档快照

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

- **行为流程**：
  1. 校验协同取消令牌、槽位名合法性与网络连接状态；
  2. 后台线程通过 `FSaveGameFileIO::ReadSlotBytes` 读取本地磁盘快照并校验头结构；
  3. 回到主线程封装 `FGameCloudSaveUploadReq` 并经 `UGameNetServiceSubsystem::SendStructRequest` 发送；
  4. 收到响应后按状态映射结算（409 → `VersionMismatch`、404 → `SlotNotFound`、其余服务端错误 → `NetworkError`），广播 `OnCloudUploadComplete` 并调用 C++ 回调。

### 3. 异步下载云端存档（先验后落盘 + 零二次读盘）

```cpp
void DownloadSlotAsync(
    const FString& SlotName,
    bool bAutoApplyToSubsystem,
    float TimeoutSeconds,
    FOnCloudDownloadCompleted Callback,
    FCancellationToken CancellationToken = FCancellationToken::None()
);

UFUNCTION(BlueprintCallable, Category = "GameData|CloudSave")
void DownloadSlot(
    const FString& SlotName,
    bool bAutoApplyToSubsystem = true,
    float TimeoutSeconds = 10.0f
);
```

- **行为流程**：
  1. 校验取消令牌、槽位名与连接态，发送 `FGameCloudSaveDownloadReq`；
  2. 收到云端字节流后，**在触碰本地磁盘前**先在主线程执行 `ValidateDownloadedBytes`（含魔数头检查 + `USaveGameSubsystem::DeserializeAndValidateArchive` 内存反序列化与归档/分段版本预检）；
     - 任一校验失败直接返回 `CorruptedData` 或 `VersionMismatch`，**本地既有健康 `.sav` 零污染**；
  3. 校验通过后签发单调代次 `IssueSlotSaveGeneration`，后台原子写入 `.sav`（失败返回 `IoError`）；
  4. 若 `bAutoApplyToSubsystem == true`，回到主线程直接将已通过校验的内存 `UGameSaveArchive` 交由 `USaveGameSubsystem::ApplyArchiveToSegments` 热应用（零二次读盘与二次反序列化）。

### 4. 异步查询云端元数据

```cpp
void QueryCloudSlotMetadataAsync(
    const FString& SlotName,
    float TimeoutSeconds,
    FOnCloudQueryCompleted Callback,
    FCancellationToken CancellationToken = FCancellationToken::None()
);
```

- 用于在下载前比对本地存档与云端存档的 `CloudTimestamp` 与 `SaveSizeBytes`；当服务端返回 `!bSuccess && ErrorCode == 404` 或 `bSuccess && !bExistsOnCloud` 时映射为 `EGameCloudSaveResult::SlotNotFound`，其余服务端失败映射为 `EGameCloudSaveResult::NetworkError`，并在回调参数 `FGameCloudSaveQueryRsp` 中完整透传服务端 `ErrorCode` 与 `ErrorMessage`。
