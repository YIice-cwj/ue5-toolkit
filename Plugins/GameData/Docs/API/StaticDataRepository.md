# FStaticDataRepository API 参考

> **一句话**：类型擦除的 `.bin` 静态数据仓库、二进制编解码器与泛型快照 RCU 存储。
> **头文件**：`StaticData/FStaticDataRepository.h`、`StaticData/FStaticDataBinCodec.h`、`StaticData/TStaticDataSnapshotStore.h`、`Core/DataErrorCodes.h` · **线程**：仓库实例非线程安全，由调用方串行；快照存储为多读单写，`GetVersion()` 无锁读取 · **失败语义**：`TGameStatusResult<T>` 或 `TGameResult<T, EStaticDataBinError>`，统一归属 `Game::ModuleId::Data`

## 30 秒速览

- `FStaticDataRepository` 对行类型零知识：行以二进制负载存储，读写由生成管线产出的 `FRowPayloadFunc` 承担，全程不经 `UScriptStruct` 反射。
- `FStaticDataBinCodec` 负责 `.bin` 编解码：Magic `TSDB`、严格格式版本 `v2`、负载 CRC32。
- `TStaticDataSnapshotStore<TSnapshot>` 提供 RCU 发布、无锁版本读取与重载门闩，版本与原子发布归它，不归仓库。
- `FDataTableLoaderUtils::LoadTableFromJson` 是 JSON 通道装载入口，支持合法空表。
- 仓库不参与运行期查询路径，仅承接 `.bin` 反序列化结果与 Cook 前置导出；纯 C++ 可脱机实例化。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `FStaticDataRepository::RegisterRowSerializer` | 登记行负载读写器 | 同结构重复登记即覆盖；拒绝空 `FName` 或空函数指针 |
| `FStaticDataRepository::AddTable<TRowType>` | 写入整表行数据 | 同结构整体覆盖；空表也要求读写器已登记 |
| `FStaticDataRepository::ExtractTable<TRowType>` | 按结构名提取整表 | 表缺失返回 `BinTableNotFound (10005)` |
| `FStaticDataRepository::ExportToFile` | 全量导出 `.bin` | 编码或写盘失败返回具体错误码 |
| `FStaticDataRepository::ImportFromFile` | 从 `.bin` 导入 | 解码到临时集合并原子换入，失败零污染；返回表数 |
| `FStaticDataBinCodec::Encode` | 编码为 `.bin` 文件字节流 | 单缓冲零二次拷贝 |
| `FStaticDataBinCodec::Decode` | 解码并校验 `.bin` | 仅接受 `FormatVersion == 2`，校验 CRC32 与尾字节 |
| `TStaticDataSnapshotStore::Acquire` | 取得在线快照共享指针 | 未发布过返回空；多读并发 |
| `TStaticDataSnapshotStore::Publish` | 原子发布新快照 | 单写；写锁外析构旧快照 |
| `FDataTableLoaderUtils::LoadTableFromJson` | 从 JSON 装载表 | 合法空表视为成功；失败返回 `10008`–`10011` |

## 错误码 `EStaticDataBinError`

```cpp
enum class EStaticDataBinError : uint8 {
    None,
    IoFailure,
    MagicMismatch,
    UnsupportedVersion,
    ChecksumMismatch,
    TableNotFound,
    RowDeserializeFailed,
    SerializerNotFound,
};
```

| 枚举值 | `Game::DataErrorCodes` | 数值 | 触发条件 |
| :--- | :--- | :---: | :--- |
| `IoFailure` | `BinIoFailure` | `10001` | 文件读写失败、截断、重复表/行键、尾字节异常 |
| `MagicMismatch` | `BinMagicMismatch` | `10002` | 文件头 magic 不匹配 |
| `UnsupportedVersion` | `BinUnsupportedVersion` | `10003` | 格式版本不受支持（仅接受 v2） |
| `ChecksumMismatch` | `BinChecksumMismatch` | `10004` | 负载 CRC32 校验失败 |
| `TableNotFound` | `BinTableNotFound` | `10005` | 按结构名查找表不存在 |
| `RowDeserializeFailed` | `BinRowDeserializeFailed` | `10006` | 行负载反序列化失败 |
| `SerializerNotFound` | `BinSerializerNotFound` | `10007` | 该结构名未注册生成式负载读写器 |

映射由 `FStaticDataBinCodec::ToDataErrorCode` / `MakeError` 完成，归属 `Game::ModuleId::Data`。

## 内容摘要 `FStaticDataContentDigest`

```cpp
struct FStaticDataContentDigest {
    int32 TableCount = 0;
    int32 RowCount = 0;
    FString Digest;
};
```

- `Digest` 为顺序无关的 SHA-1；空仓库返回空输入摘要 `da39a3ee5e6b4b0d3255bfef95601890afd80709`。
- 表按结构名、行按行键字典序排序后计入摘要；结构名、行键与行负载均以长度前缀（`uint32` 字节长 + UTF-8 / 原始负载字节）喂入 SHA-1，杜绝相邻字段拼接碰撞。
- 跨平台（Windows / Mac / Linux）保证 `.bin` 与 JSON 双通道对拍摘要一致。

## 文件格式

```text
[magic:4 (TSDB)] [格式版本:4 (严格 v2)] [表数:4]
  { [结构名 FString] [行数:4] { [行键 FString] [行负载长度:4] [行负载字节] } }
[CRC32:4]
```

- `Encode` 单缓冲零二次拷贝：预估容量后直写 Magic、表体与末尾 CRC32。
- `Decode` 严格校验 `FormatVersion == 2`（拒绝 v0 / v1 / >2）、CRC32，以及解析完成后零未消费尾字节。
- **CRC32 仅覆盖负载**：校验范围是 Magic 之后的负载区（格式版本 + 表体），不包含文件头 Magic 与尾部 CRC 字段本身。

## 泛型数据仓库 `FStaticDataRepository`

```cpp
class GAMEDATACORE_API FStaticDataRepository {
public:
    using FRowPayloadFunc = bool (*)(FArchive& Ar, void* RowPtr);

    void RegisterRowSerializer(const FName& StructName, FRowPayloadFunc PayloadFunc);

    template <typename TRowType>
    void RegisterRowSerializer(FRowPayloadFunc PayloadFunc);

    template <typename TRowType>
    [[nodiscard]] TGameStatusResult<void> AddTable(const TMap<FName, TRowType>& InRows);

    template <typename TRowType>
    [[nodiscard]] TGameStatusResult<TMap<FName, TRowType>> ExtractTable() const;

    [[nodiscard]] int32 TableCount() const noexcept;
    void Reset() noexcept;

    [[nodiscard]] TGameStatusResult<void> ExportToFile(const FString& FilePath) const;
    [[nodiscard]] TGameStatusResult<int32> ImportFromFile(const FString& FilePath);

    [[nodiscard]] FStaticDataContentDigest ComputeContentDigest() const;
};
```

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `RegisterRowSerializer` | `StructName` / `PayloadFunc` | `void` | 同结构重复登记即覆盖；拒绝空 `FName` 或空函数指针 |
| `AddTable<TRowType>` | `InRows` | `TGameStatusResult<void>` | 同结构重复写入即整体覆盖；空表也校验读写器已登记 |
| `ExtractTable<TRowType>` | 无 | `TGameStatusResult<TMap<FName, TRowType>>` | 按 `StaticStruct()->GetName()` 匹配镜像；表缺失返回 `BinTableNotFound` |
| `ExportToFile` | `FilePath` | `TGameStatusResult<void>` | 全量导出，编码或写盘失败返回具体错误码 |
| `ImportFromFile` | `FilePath` | `TGameStatusResult<int32>` | 成功返回装载表数；解析到临时集合后原子换入，失败零污染 |

- 全部错误通过返回值精确返回，不使用 `GetLastError()` 之类的状态字段。
- 推荐使用模板重载 `RegisterRowSerializer<TRowType>(&Func)`，自动取结构名避免字面量漂移。
- 行读写器在循环外单次解析，热循环不重复做哈希查找。

### 使用示例

```cpp
#include "StaticData/FStaticDataRepository.h"
#include "Generated/Data/StaticData/StaticDataSerialization.h"

FStaticDataRepository Repository;
RegisterGameStaticDataRowSerializers(Repository);

TMap<FName, FItemRow> Rows;
if (Repository.AddTable(Rows).IsSuccess() && Repository.ExportToFile(BinPath).IsSuccess()) {
    GAME_LOG(Log, TEXT("Exported %d tables"), Repository.TableCount());
}
```

## 二进制编解码器 `FStaticDataBinCodec`

```cpp
class GAMEDATACORE_API FStaticDataBinCodec {
public:
    FStaticDataBinCodec() = delete;

    [[nodiscard]] static TGameResult<TArray<uint8>, EStaticDataBinError> Encode(const TMap<FName, FStaticDataBinaryTable>& InTables);
    [[nodiscard]] static TGameResult<TMap<FName, FStaticDataBinaryTable>, EStaticDataBinError> Decode(const TArray<uint8>& InFileBytes);
    [[nodiscard]] static FStaticDataContentDigest ComputeDigest(const TMap<FName, FStaticDataBinaryTable>& InTables);
    [[nodiscard]] static FString DescribeError(EStaticDataBinError InError);
    [[nodiscard]] static int32 ToDataErrorCode(EStaticDataBinError InError) noexcept;
    [[nodiscard]] static FGameError MakeError(EStaticDataBinError InError);
};
```

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `Encode` | `InTables` | `TGameResult<TArray<uint8>, EStaticDataBinError>` | 产出完整文件字节流 |
| `Decode` | `InFileBytes` | `TGameResult<TMap<FName, FStaticDataBinaryTable>, EStaticDataBinError>` | 校验 Magic、精确版本、CRC32 与尾字节 |
| `ComputeDigest` | `InTables` | `FStaticDataContentDigest` | 求解顺序无关摘要 |
| `ToDataErrorCode` / `MakeError` | `InError` | `int32` / `FGameError` | 映射至 `Game::DataErrorCodes` 与标准错误对象 |

## 泛型快照 RCU 存储 `TStaticDataSnapshotStore<TSnapshot>`

```cpp
template <typename TSnapshot>
concept CStaticDataSnapshot = requires(const TSnapshot& Snapshot) {
    { Snapshot.DataVersion } -> std::convertible_to<uint32>;
};

template <CStaticDataSnapshot TSnapshot>
class TStaticDataSnapshotStore {
public:
    class FReloadLatchTicket {
    public:
        FReloadLatchTicket(TSharedRef<std::atomic<uint64>, ESPMode::ThreadSafe> InLatch, uint64 InToken) noexcept;
        ~FReloadLatchTicket();
        void Release() noexcept;
    };

    [[nodiscard]] TSharedPtr<const TSnapshot> Acquire() const;
    [[nodiscard]] uint32 GetVersion() const noexcept;
    void Publish(TSharedPtr<const TSnapshot> NextSnapshot);
    [[nodiscard]] TGameStatusResult<void> TryBeginReload();
    [[nodiscard]] TSharedRef<FReloadLatchTicket, ESPMode::ThreadSafe> MakeReloadLatchTicket() const;
    void EndReload() noexcept;
    [[nodiscard]] bool IsReloadInProgress() const noexcept;
};
```

| 方法 | 返回 | 说明 |
| :--- | :--- | :--- |
| `Acquire` | `TSharedPtr<const TSnapshot>` | 瞬态读锁拷贝在线快照共享指针后立即出锁；未发布过返回空 |
| `GetVersion` | `uint32` | 无锁原子读（`std::memory_order_acquire`）；未发布过返回 `0` |
| `Publish` | `void` | 写锁内换指针并更新版本，写锁外析构旧快照 |
| `TryBeginReload` | `TGameStatusResult<void>` | CAS 抢占门闩；已有重载进行中返回 `ReloadInProgress (10012)` |
| `MakeReloadLatchTicket` | `TSharedRef<FReloadLatchTicket, ESPMode::ThreadSafe>` | 生成跨线程 RAII 票据，绑定当轮代次 Token |
| `FReloadLatchTicket::Release` | `void` | 按代次 Token CAS 幂等释放，免疫 ABA 重入误释放 |
| `EndReload` / `IsReloadInProgress` | `void` / `bool` | 释放门闩 / 查询门闩占用状态 |

- 多读并发、单写发布；禁止拷贝与移动。
- `Publish` 以快照的 `DataVersion` 作为新版本号，空指针发布时版本归零。

## JSON 装载工具 `FDataTableLoaderUtils`

```cpp
class FDataTableLoaderUtils {
public:
    FDataTableLoaderUtils() = delete;

    template <typename TRowType>
    [[nodiscard]] static TGameStatusResult<TMap<FName, TRowType>> LoadTableFromJson(const FString& FilePath);
};
```

- 行键优先取 `RowName`，回退 `Name`；重复键、非对象行、字段反序列化失败均计入失败。
- 失败返回 `JsonFileNotFound (10008)` / `JsonReadFailed (10009)` / `JsonParseFailed (10010)` / `JsonRowValidationFailed (10011)`。
- 内部使用 `MoveTemp` 装配行数据，合法空表 `[]` 视为加载成功。

## 常见错误与边界

- **读写器未登记**：`AddTable` / `ExtractTable` 即使传入空表也要求读写器已登记，否则返回 `BinSerializerNotFound (10007)`。
- **表不存在**：`ExtractTable` 对未写入的结构名返回 `BinTableNotFound (10005)`，不会返回空表。
- **重复与脏尾**：重复表名、重复行键或解析后仍有未消费字节统一归为 `BinIoFailure (10001)`。
- **版本门槛**：`Decode` 只接受 `FormatVersion == 2`，其余返回 `BinUnsupportedVersion (10003)`。
- **CRC 范围**：CRC32 只覆盖 Magic 之后的负载区，Magic 本身由独立比较校验，损坏时返回 `MagicMismatch (10002)`。
- **状态污染**：`ImportFromFile` 先解码到临时集合再原子换入，失败时仓库保持原状。

## 相关文档

- [插件 README](../../README.md)
- [静态数据管线](../Architecture/StaticDataPipeline.md)
- [快速上手](../Guides/QuickStart.md)
- [静态数据子系统 API](StaticDataSubsystem.md)
