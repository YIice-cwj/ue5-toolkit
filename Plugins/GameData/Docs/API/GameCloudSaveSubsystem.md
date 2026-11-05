# GameCloudSaveSubsystem API 参考

`UGameCloudSaveSubsystem` 是 `GameData` 模块对业务层暴露的云端存档同步门面。它基于 `UGameNetServiceSubsystem` 的强类型 RPC 双向请求通道，并结合底层 `FSaveGameFileIO` 与 `USaveGameSubsystem` 实现本地快照上传、云端拉取、合法性拦截与代次原子安全落盘。

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
    Success,       // 操作成功（上传完成 / 下载落盘完成）
    NetworkError,  // 网络不可用、连接断开或 RPC 超时
    SlotNotFound,  // 本地无此槽位文件，或云端不存在该存档
    CorruptedData  // 数据校验失败（损坏的魔数/头结构拦截）
};
```

---

## 请求与响应结构体

### 上传协议
- **`FGameCloudSaveUploadReq`** (MessageId: 201)
  - `FString SlotName`: 目标存档槽位名
  - `int32 ArchiveVersion`: 归档版本
  - `FDateTime SaveTimestamp`: 本地保存时间戳 (UTC)
  - `TArray<uint8> SaveBytes`: 原始存档字节流
- **`FGameCloudSaveUploadRsp`** (MessageId: 202)
  - `bool bSuccess`: 服务端是否接受
  - `FString SlotName`: 存档槽位名
  - `int32 ErrorCode`: 错误码（0 表示成功）
  - `FString ErrorMessage`: 错误信息
  - `FDateTime CloudTimestamp`: 服务端落库时间戳

### 下载协议
- **`FGameCloudSaveDownloadReq`** (MessageId: 203)
  - `FString SlotName`: 目标存档槽位名
- **`FGameCloudSaveDownloadRsp`** (MessageId: 204)
  - `bool bSuccess`: 服务端是否找到存档
  - `FString SlotName`: 存档槽位名
  - `int32 ErrorCode`: 错误码（0 表示成功，404 表示不存在）
  - `FString ErrorMessage`: 错误信息
  - `int32 ArchiveVersion`: 云端归档版本
  - `FDateTime CloudTimestamp`: 云端保存时间戳
  - `TArray<uint8> SaveBytes`: 原始存档字节流

### 元数据查询协议
- **`FGameCloudSaveQueryReq`** (MessageId: 205)
  - `FString SlotName`: 目标存档槽位名
- **`FGameCloudSaveQueryRsp`** (MessageId: 206)
  - `bool bSuccess`: 查询是否成功
  - `bool bExistsOnCloud`: 云端是否存在该槽位
  - `FString SlotName`: 存档槽位名
  - `int32 ArchiveVersion`: 云端归档版本
  - `FDateTime CloudTimestamp`: 云端保存时间戳
  - `int64 SaveSizeBytes`: 存档字节大小

---

## 类声明核心接口

### 1. 静态寻址
```cpp
static UGameCloudSaveSubsystem* GetPtr(const UObject* WorldContextObject);
static UGameCloudSaveSubsystem& Get(const UObject* WorldContextObject);
```
支持从 `UWorld`、`AActor`、`UActorComponent` 或 `UGameInstance` 寻址。

### 2. 异步上传本地存档快照
```cpp
void UploadSlotAsync(
    const FString& SlotName,
    float TimeoutSeconds,
    FOnCloudUploadCompleted Callback
);

// 蓝图便捷调用版本（广播 OnCloudUploadComplete）
UFUNCTION(BlueprintCallable, Category = "GameData|CloudSave")
void UploadSlot(const FString& SlotName, float TimeoutSeconds = 10.0f);
```

- **行为流程**：
  1. 通过 `FSaveGameFileIO::ReadSlotBytes` 读取本地磁盘快照；
  2. 校验文件魔数与头结构 (`HasValidSaveGameHeader`)；
  3. 包装 `FGameCloudSaveUploadReq`，调用 `UGameNetServiceSubsystem::SendStructRequest`；
  4. 收到应答或超时，分发委托及调用 C++ 回调。

### 3. 异步下载云端存档与原子落盘
```cpp
void DownloadSlotAsync(
    const FString& SlotName,
    bool bAutoApplyToSubsystem,
    float TimeoutSeconds,
    FOnCloudDownloadCompleted Callback
);

// 蓝图便捷调用版本（广播 OnCloudDownloadComplete）
UFUNCTION(BlueprintCallable, Category = "GameData|CloudSave")
void DownloadSlot(
    const FString& SlotName,
    bool bAutoApplyToSubsystem = true,
    float TimeoutSeconds = 10.0f
);
```

- **行为流程**：
  1. 发送 `FGameCloudSaveDownloadReq` 至远端；
  2. 收到应答后，在向本地落盘前**严格执行** `FSaveGameFileIO::HasValidSaveGameHeader` 校验；若头部损坏则直接拒绝并返回 `CorruptedData`，绝不污染本地磁盘；
  3. 签发单调代次 `IssueSlotSaveGeneration`，调用 `FSaveGameFileIO::WriteSlotBytes` 安全原子写入；
  4. 若 `bAutoApplyToSubsystem == true`，自动通过 `USaveGameSubsystem::LoadGameFromSlot` 反解并更新当前已注册的内存段数据。

### 4. 异步查询云端元数据
```cpp
void QueryCloudSlotMetadataAsync(
    const FString& SlotName,
    float TimeoutSeconds,
    FOnCloudQueryCompleted Callback
);
```

- 用于在下载前比对本地存档与云端存档的 `CloudTimestamp`，提示玩家“云端存档较新/较旧”以及防止意外覆盖。

