# GameData 二进制数据仓库 API 参考

`FStaticDataRepository` 是类型擦除的 `.bin` 静态数据仓库：对所有行类型零知识，行负载读写由生成管线产出的序列化器承担。纯 C++ 实现，不依赖 `UWorld` / `UGameInstance`，可在脱机脚本与测试中实例化。

- 头文件：`#include "StaticData/FStaticDataRepository.h"`
- 模块：`GameDataCore`（`GAMEDATACORE_API`）
- 边界：仓库不携带数据版本（版本归 `FStaticDataSnapshot`），不参与运行期查询路径，仅承接 `.bin` 反序列化结果与 Cook 前置导出。

---

## 1. 错误码 `EStaticDataBinError`

```cpp
enum class EStaticDataBinError : uint8 {
    None,
    IoFailure,            // 文件不存在 / 读取失败 / 结构截断 / 负载写出失败
    MagicMismatch,        // 文件头 magic 不匹配
    UnsupportedVersion,   // 格式版本高于当前支持版本
    ChecksumMismatch,     // 负载 CRC32 校验失败
    TableNotFound,        // 按结构名查找表不存在
    RowDeserializeFailed, // 行负载反序列化失败（内容损坏）
    SerializerNotFound,   // 该结构名未注册生成式负载读写器
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

表按结构名、行按行键字典序排序后计入摘要，结构名与行键统一通过 `FTCHARToUTF8` 转为标准 UTF-8 字节流参与 SHA-1 计算，与装载顺序解耦且跨平台（Windows/Mac/Linux）保证二进制对拍摘要绝对统一；同一批数据无论经 `.bin` 还是 JSON 通道装载，摘要必须一致。

---

## 3. 文件格式

```text
[magic:4 (TSDB)] [格式版本:4 (v2)] [表数:4]
  { [结构名 FString] [行数:4] { [行键 FString] [行负载长度:4] [行负载字节] } }
[CRC32:4]
```

- 平台端序直写，不作跨端序承诺。
- CRC32 覆盖自格式版本起至末行负载的全部字节；校验不符即整体拒绝。

---

## 4. 类接口

### 4.1 泛型数据仓库 `FStaticDataRepository`

```cpp
class GAMEDATACORE_API FStaticDataRepository {
public:
    using FRowPayloadFunc = bool (*)(FArchive& Ar, void* RowPtr);

    void RegisterRowSerializer(const FName& StructName, FRowPayloadFunc PayloadFunc);

    template<typename RowType>
    [[nodiscard]] bool AddTable(const TMap<FName, RowType>& InRows);

    template<typename RowType>
    [[nodiscard]] bool ExtractTable(TMap<FName, RowType>& OutRows) const;

    [[nodiscard]] int32 TableCount() const noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool ExportToFile(const FString& FilePath) const;
    [[nodiscard]] bool ImportFromFile(const FString& FilePath);

    [[nodiscard]] EStaticDataBinError GetLastError() const noexcept;
    [[nodiscard]] FStaticDataContentDigest ComputeContentDigest() const;
};
```

### 4.2 二进制编解码器 `FStaticDataBinCodec`

```cpp
class GAMEDATACORE_API FStaticDataBinCodec {
public:
    [[nodiscard]] static bool Encode(const TMap<FName, FStaticDataBinaryTable>& InTables, TArray<uint8>& OutFileBytes);
    [[nodiscard]] static EStaticDataBinError Decode(const TArray<uint8>& InFileBytes, TMap<FName, FStaticDataBinaryTable>& OutTables);
    [[nodiscard]] static FStaticDataContentDigest ComputeDigest(const TMap<FName, FStaticDataBinaryTable>& InTables);
    [[nodiscard]] static FString DescribeError(EStaticDataBinError InError);
};
```

### 4.3 数据表 JSON 装载工具 `FDataTableLoaderUtils`

```cpp
class FDataTableLoaderUtils {
public:
    FDataTableLoaderUtils() = delete;

    template <typename TRowType>
    [[nodiscard]] static bool LoadTableFromJson(const FString& FilePath, TMap<FName, TRowType>& OutMap);
};
```

- `LoadTableFromJson` 在入口统一清空 `OutMap`，失败时不残留半填充脏数据；
- 内部采用 `MoveTemp` 零拷贝装配行数据，消除大结构体反序列化过程中的多余深拷贝。

### 4.4 序列化器注册与装载细节
- `StructName` 必须与 `RowType::StaticStruct()->GetName()` 严格一致。
- 生成管线为每张表产出 `Serialize{Stem}Row(FArchive&, void*)`，并在注册入口统一登记。
- 同结构重复登记即覆盖；重复 `AddTable` 即整表覆盖。
- `ImportFromFile` 内部委托 `FStaticDataBinCodec::Decode` 先解析到临时表集合，全部成功后原子换入，失败零污染。
- `GetLastError()` 记录最近一次失败原因，供日志与验收脚本定位。
- `Reset()` 清空数据与错误码，已登记序列化器保留。

---

## 5. 使用示例

```cpp
#include "StaticData/FStaticDataRepository.h"
#include "Generated/Data/StaticData/StaticDataSerialization.h"

FStaticDataRepository Repository;
RegisterGameStaticDataRowSerializers(Repository);

TMap<FName, FItemRow> Rows;
// Rows 由 JSON 装载或业务侧填充
if (Repository.AddTable(Rows) && Repository.ExportToFile(BinPath))
{
    GAME_LOG(Log, TEXT("Exported %d tables"), Repository.TableCount());
}
```

生成式序列化器位于 `StaticDataSerialization.cpp`，统一由 `RegisterGameStaticDataRowSerializers(FStaticDataRepository&)` 登记，业务侧不要手写同名函数。
