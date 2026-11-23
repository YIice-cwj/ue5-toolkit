# GameData 二进制数据仓库与快照存储 API 参考

`FStaticDataRepository` 是类型擦除的 `.bin` 静态数据仓库：对所有行类型零知识，行负载读写由生成管线产出的序列化器承担。纯 C++ 实现，不依赖 `UWorld` / `UGameInstance`，可在脱机脚本与测试中实例化。

- 头文件：`#include "StaticData/FStaticDataRepository.h"`、`#include "StaticData/TStaticDataSnapshotStore.h"`、`#include "Core/DataErrorCodes.h"`
- 模块：`GameDataCore`（`GAMEDATACORE_API`）
- 边界：仓库不携带数据版本（版本与 RCU 发布归 `TStaticDataSnapshotStore<TSnapshot>`），不参与运行期查询路径，仅承接 `.bin` 反序列化结果与 Cook 前置导出。

---

## 1. 错误码体系

底层编解码枚举 `EStaticDataBinError` 经 `FStaticDataBinCodec::ToDataErrorCode` / `MakeError` 统一映射至 `Game::ModuleId::Data` 与 `Game::DataErrorCodes::Bin*`（`10001`–`10007`）：

```cpp
enum class EStaticDataBinError : uint8 {
    None,
    IoFailure,            // BinIoFailure (10001): 文件读写失败、截断、重复表/键、尾字节异常
    MagicMismatch,        // BinMagicMismatch (10002): 文件头 magic 不匹配
    UnsupportedVersion,   // BinUnsupportedVersion (10003): 格式版本不受支持（仅接受 v2）
    ChecksumMismatch,     // BinChecksumMismatch (10004): 负载 CRC32 校验失败
    TableNotFound,        // BinTableNotFound (10005): 按结构名查找表不存在
    RowDeserializeFailed, // BinRowDeserializeFailed (10006): 行负载反序列化失败
    SerializerNotFound,   // BinSerializerNotFound (10007): 该结构名未注册生成式负载读写器
};
```

---

## 2. 内容摘要 `FStaticDataContentDigest`

```cpp
struct FStaticDataContentDigest {
    int32 TableCount = 0;
    int32 RowCount = 0;
    FString Digest;   // 顺序无关的 SHA-1 摘要，空仓库为空输入摘要（da39a3ee5e6b4b0d3255bfef95601890afd80709）
};
```

表按结构名、行按行键字典序排序后计入摘要；结构名、行键与行负载均以**长度前缀**（`uint32` 字节长 + UTF-8/原始负载字节）喂入 SHA-1，杜绝相邻字段拼接碰撞，且跨平台（Windows/Mac/Linux）保证 `.bin` 与 JSON 双通道对拍摘要完全一致。

---

## 3. 文件格式

```text
[magic:4 (TSDB)] [格式版本:4 (严格 v2)] [表数:4]
  { [结构名 FString] [行数:4] { [行键 FString] [行负载长度:4] [行负载字节] } }
[CRC32:4]
```

- 单缓冲零二次拷贝编码（`Encode` 预估容量直写 Magic、表体与末尾 CRC32）。
- `Decode` 严格校验 `FormatVersion == 2`（拒绝 v0/v1/>2）、全包 CRC32 以及解析完成后零未消费尾字节。

---

## 4. 类接口

### 4.1 泛型数据仓库 `FStaticDataRepository`

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

- 全部错误均通过返回值 `TGameStatusResult<T>` 精确返回（已移除历史 `GetLastError()` 状态字段，消除无锁并发与状态残留风险）。
- `RegisterRowSerializer` 拒绝空 `FName` 或空函数指针；推荐优先使用模板重载 `RegisterRowSerializer<TRowType>(&Func)`。
- `AddTable` / `ExtractTable` 在循环外单次解析读写器，即便空表也严格校验读写器是否已注册。
- `ImportFromFile` 内部委托 `FStaticDataBinCodec::Decode` 解析到临时集合后原子换入，失败零污染。

### 4.2 二进制编解码器 `FStaticDataBinCodec`

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

### 4.3 泛型快照 RCU 存储 `TStaticDataSnapshotStore<TSnapshot>`

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
        void Release() noexcept; // 按代次 Token CAS 幂等释放，免疫 ABA 重入误释放
    };

    [[nodiscard]] TSharedPtr<const TSnapshot> Acquire() const;
    [[nodiscard]] uint32 GetVersion() const noexcept; // 无锁原子读（std::memory_order_acquire）
    void Publish(TSharedPtr<const TSnapshot> NextSnapshot);
    [[nodiscard]] TGameStatusResult<void> TryBeginReload(); // 冲突返回 ReloadInProgress (10012)
    [[nodiscard]] TSharedRef<FReloadLatchTicket, ESPMode::ThreadSafe> MakeReloadLatchTicket() const;
    void EndReload() noexcept;
    [[nodiscard]] bool IsReloadInProgress() const noexcept;
};
```

### 4.4 数据表 JSON 装载工具 `FDataTableLoaderUtils`

```cpp
class FDataTableLoaderUtils {
public:
    FDataTableLoaderUtils() = delete;

    template <typename TRowType>
    [[nodiscard]] static TGameStatusResult<TMap<FName, TRowType>> LoadTableFromJson(const FString& FilePath);
};
```

- 失败时返回归属 `Game::ModuleId::Data` 的 `JsonFileNotFound (10008)` / `JsonReadFailed (10009)` / `JsonParseFailed (10010)` / `JsonRowValidationFailed (10011)`。
- 内部采用 `MoveTemp` 零拷贝装配行数据，支持合法空表 `[]`。

---

## 5. 使用示例

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
