/**
 * @file FStaticDataBinCodec.cpp
 * @brief .bin 文件格式编解码器实现
 */

#include "StaticData/FStaticDataBinCodec.h"

#include "Core/DataErrorCodes.h"
#include "Misc/Crc.h"
#include "Misc/SecureHash.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/NameTypes.h"

namespace {

constexpr uint32 StaticDataBinMagic = (uint32('T') << 24) | (uint32('S') << 16) | (uint32('D') << 8) | uint32('B');
constexpr uint32 StaticDataBinFormatVersion = 2;
constexpr int32 MagicByteSize = static_cast<int32>(sizeof(StaticDataBinMagic));
constexpr int32 VersionByteSize = static_cast<int32>(sizeof(StaticDataBinFormatVersion));
constexpr int32 TableCountByteSize = static_cast<int32>(sizeof(int32));
constexpr int32 TrailerCrcByteSize = static_cast<int32>(sizeof(uint32));
constexpr int32 MinValidFileByteSize = MagicByteSize + VersionByteSize + TableCountByteSize + TrailerCrcByteSize;
constexpr int64 MinSerializedNonEmptyStringBytes = static_cast<int64>(sizeof(int32)) + 2;
constexpr int64 MinTableRecordBytes = MinSerializedNonEmptyStringBytes + static_cast<int64>(sizeof(int32));
constexpr int64 MinRowRecordBytes = MinSerializedNonEmptyStringBytes + static_cast<int64>(sizeof(int32));

/**
 * @brief 计算内存读取器当前游标之后的剩余可读字节数
 * @param Reader 内存读取器引用
 * @return 剩余字节数
 */
int64 RemainingBytes(FMemoryReader& Reader) {
    return Reader.TotalSize() - Reader.Tell();
}

/**
 * @brief 估算编码后完整文件字节容量，减少 FMemoryWriter 写入过程中的数组扩容拷贝
 * @param InTables 待编码的表集合
 * @return 估算的总字节容量
 */
int32 EstimateEncodedFileSize(const TMap<FName, FStaticDataBinaryTable>& InTables) {
    int64 EstimatedBytes = MinValidFileByteSize;
    for (const TPair<FName, FStaticDataBinaryTable>& Pair : InTables) {
        const FStaticDataBinaryTable& Table = Pair.Value;
        EstimatedBytes += static_cast<int64>(sizeof(int32)) + Table.StructName.Len() + 1 + static_cast<int64>(sizeof(int32));
        for (const TPair<FName, TArray<uint8>>& Row : Table.Rows) {
            EstimatedBytes += static_cast<int64>(sizeof(int32)) * 2 + 32 + Row.Value.Num();
        }
    }
    return static_cast<int32>(FMath::Min<int64>(EstimatedBytes, MAX_int32));
}

/**
 * @brief 将 UTF-8 字节段附带 32 位无符号字节长度前缀并入 SHA-1 摘要流
 * @param Hasher SHA-1 计算器引用
 * @param Data 字节起始指针
 * @param ByteLength 字节长度
 */
void UpdateHashLengthPrefixed(FSHA1& Hasher, const uint8* Data, uint32 ByteLength) {
    Hasher.Update(reinterpret_cast<const uint8*>(&ByteLength), static_cast<uint32>(sizeof(ByteLength)));
    if (ByteLength > 0 && Data != nullptr) {
        Hasher.Update(Data, ByteLength);
    }
}

/**
 * @brief 将 20 字节 SHA-1 原始摘要零临时分配格式化为 40 位小写十六进制字符串
 * @param DigestBytes 固定长度为 FSHA1::DigestSize 的原始摘要字节数组
 * @return 小写十六进制摘要字符串
 */
FString FormatSha1HexLower(const uint8 (&DigestBytes)[FSHA1::DigestSize]) {
    static constexpr TCHAR HexDigits[] = TEXT("0123456789abcdef");
    TCHAR HexBuffer[FSHA1::DigestSize * 2 + 1];
    for (int32 Index = 0; Index < FSHA1::DigestSize; ++Index) {
        const uint8 Byte = DigestBytes[Index];
        HexBuffer[Index * 2] = HexDigits[(Byte >> 4) & 0x0F];
        HexBuffer[Index * 2 + 1] = HexDigits[Byte & 0x0F];
    }
    HexBuffer[FSHA1::DigestSize * 2] = TEXT('\0');
    return FString(FSHA1::DigestSize * 2, HexBuffer);
}

}  // namespace

/**
 * @brief 将表集合编码为完整文件字节流（单缓冲直写：Magic + 格式版本 + 表集合 + CRC32）
 * @param InTables 表集合
 * @return 成功返回完整文件字节流，失败返回具体错误码
 */
TGameResult<TArray<uint8>, EStaticDataBinError> FStaticDataBinCodec::Encode(const TMap<FName, FStaticDataBinaryTable>& InTables) {
    TArray<FName> SortedStructNames;
    InTables.GetKeys(SortedStructNames);
    SortedStructNames.Sort(FNameLexicalLess());

    for (const FName& StructKey : SortedStructNames) {
        if (StructKey.IsNone()) {
            return TGameResult<TArray<uint8>, EStaticDataBinError>::Failure(EStaticDataBinError::IoFailure);
        }
        const FStaticDataBinaryTable& Table = InTables.FindChecked(StructKey);
        if (!Table.StructName.IsEmpty() && FName(*Table.StructName) != StructKey) {
            return TGameResult<TArray<uint8>, EStaticDataBinError>::Failure(EStaticDataBinError::IoFailure);
        }
    }

    TArray<uint8> OutFileBytes;
    OutFileBytes.Reserve(EstimateEncodedFileSize(InTables));
    OutFileBytes.SetNumUninitialized(MagicByteSize);
    FMemory::Memcpy(OutFileBytes.GetData(), &StaticDataBinMagic, MagicByteSize);

    FMemoryWriter PayloadWriter(OutFileBytes, false, true);
    PayloadWriter.Seek(MagicByteSize);

    uint32 FormatVersion = StaticDataBinFormatVersion;
    PayloadWriter << FormatVersion;
    int32 TotalTableCount = SortedStructNames.Num();
    PayloadWriter << TotalTableCount;
    FString StructName;
    FString RowKeyText;
    TArray<FName> SortedRowKeys;
    for (const FName& StructKey : SortedStructNames) {
        const FStaticDataBinaryTable& Table = InTables.FindChecked(StructKey);
        StructKey.ToString(StructName);
        PayloadWriter << StructName;

        SortedRowKeys.Reset();
        Table.Rows.GetKeys(SortedRowKeys);
        SortedRowKeys.Sort(FNameLexicalLess());

        int32 RowCount = SortedRowKeys.Num();
        PayloadWriter << RowCount;
        for (const FName& RowKey : SortedRowKeys) {
            if (RowKey.IsNone()) {
                return TGameResult<TArray<uint8>, EStaticDataBinError>::Failure(EStaticDataBinError::IoFailure);
            }
            const TArray<uint8>& RowPayload = Table.Rows.FindChecked(RowKey);
            RowKey.ToString(RowKeyText);
            PayloadWriter << RowKeyText;
            int32 PayloadLength = RowPayload.Num();
            PayloadWriter << PayloadLength;
            if (PayloadLength > 0) {
                PayloadWriter.Serialize(const_cast<uint8*>(RowPayload.GetData()), PayloadLength);
            }
        }
    }
    if (PayloadWriter.IsError()) {
        return TGameResult<TArray<uint8>, EStaticDataBinError>::Failure(EStaticDataBinError::IoFailure);
    }

    const int32 PayloadLength = OutFileBytes.Num() - MagicByteSize;
    const uint32 PayloadCrc = FCrc::MemCrc32(OutFileBytes.GetData() + MagicByteSize, PayloadLength);
    OutFileBytes.Append(reinterpret_cast<const uint8*>(&PayloadCrc), TrailerCrcByteSize);
    return TGameResult<TArray<uint8>, EStaticDataBinError>::Success(MoveTemp(OutFileBytes));
}

/**
 * @brief 解码完整文件字节流为表集合（严格校验 Magic、精确格式版本、CRC32 与无尾随脏字节）
 * @param InFileBytes 完整文件字节流
 * @return 成功返回解码后的表集合，失败返回具体错误码
 */
TGameResult<TMap<FName, FStaticDataBinaryTable>, EStaticDataBinError> FStaticDataBinCodec::Decode(const TArray<uint8>& InFileBytes) {
    if (InFileBytes.Num() < MinValidFileByteSize) {
        return TGameResult<TMap<FName, FStaticDataBinaryTable>, EStaticDataBinError>::Failure(EStaticDataBinError::IoFailure);
    }

    uint32 Magic = 0;
    FMemory::Memcpy(&Magic, InFileBytes.GetData(), MagicByteSize);
    if (Magic != StaticDataBinMagic) {
        return TGameResult<TMap<FName, FStaticDataBinaryTable>, EStaticDataBinError>::Failure(EStaticDataBinError::MagicMismatch);
    }

    const uint8* PayloadStart = InFileBytes.GetData() + MagicByteSize;
    const int32 PayloadLength = InFileBytes.Num() - MagicByteSize - TrailerCrcByteSize;
    uint32 StoredCrc = 0;
    FMemory::Memcpy(&StoredCrc, PayloadStart + PayloadLength, TrailerCrcByteSize);
    if (FCrc::MemCrc32(PayloadStart, PayloadLength) != StoredCrc) {
        return TGameResult<TMap<FName, FStaticDataBinaryTable>, EStaticDataBinError>::Failure(EStaticDataBinError::ChecksumMismatch);
    }

    FMemoryReader PayloadReader(InFileBytes);
    PayloadReader.SetLimitSize(MagicByteSize + PayloadLength);
    PayloadReader.ArMaxSerializeSize = FMath::Max<int64>(1, PayloadLength);
    PayloadReader.Seek(MagicByteSize);
    uint32 FormatVersion = 0;
    PayloadReader << FormatVersion;
    if (PayloadReader.IsError()) {
        return TGameResult<TMap<FName, FStaticDataBinaryTable>, EStaticDataBinError>::Failure(EStaticDataBinError::IoFailure);
    }
    if (FormatVersion != StaticDataBinFormatVersion) {
        return TGameResult<TMap<FName, FStaticDataBinaryTable>, EStaticDataBinError>::Failure(EStaticDataBinError::UnsupportedVersion);
    }

    TMap<FName, FStaticDataBinaryTable> IncomingTables;
    if (!ParseTables(PayloadReader, IncomingTables)) {
        return TGameResult<TMap<FName, FStaticDataBinaryTable>, EStaticDataBinError>::Failure(EStaticDataBinError::IoFailure);
    }

    if (RemainingBytes(PayloadReader) != 0) {
        return TGameResult<TMap<FName, FStaticDataBinaryTable>, EStaticDataBinError>::Failure(EStaticDataBinError::IoFailure);
    }

    return TGameResult<TMap<FName, FStaticDataBinaryTable>, EStaticDataBinError>::Success(MoveTemp(IncomingTables));
}

/**
 * @brief 计算与装载顺序无关且带字段长度定界的内容摘要
 * @param InTables 表集合
 * @return 表数、总行数与摘要；空集合返回 SHA-1 空输入摘要
 */
FStaticDataContentDigest FStaticDataBinCodec::ComputeDigest(const TMap<FName, FStaticDataBinaryTable>& InTables) {
    TArray<FName> SortedStructNames;
    InTables.GetKeys(SortedStructNames);
    SortedStructNames.Sort(FNameLexicalLess());

    FSHA1 Hasher;
    FStaticDataContentDigest Result;
    Result.TableCount = InTables.Num();
    FString StructNameText;
    FString RowKeyText;
    TArray<FName> SortedRowKeys;
    for (const FName& StructName : SortedStructNames) {
        const FStaticDataBinaryTable& Table = InTables.FindChecked(StructName);
        StructName.ToString(StructNameText);
        const FTCHARToUTF8 StructNameUtf8(*StructNameText);
        const int32 RowCount = Table.Rows.Num();
        UpdateHashLengthPrefixed(Hasher, reinterpret_cast<const uint8*>(StructNameUtf8.Get()), static_cast<uint32>(StructNameUtf8.Length()));
        Hasher.Update(reinterpret_cast<const uint8*>(&RowCount), static_cast<uint32>(sizeof(RowCount)));

        SortedRowKeys.Reset();
        Table.Rows.GetKeys(SortedRowKeys);
        SortedRowKeys.Sort(FNameLexicalLess());
        Result.RowCount += SortedRowKeys.Num();
        for (const FName& RowKey : SortedRowKeys) {
            const TArray<uint8>& Payload = Table.Rows.FindChecked(RowKey);
            RowKey.ToString(RowKeyText);
            const FTCHARToUTF8 RowKeyUtf8(*RowKeyText);
            UpdateHashLengthPrefixed(Hasher, reinterpret_cast<const uint8*>(RowKeyUtf8.Get()), static_cast<uint32>(RowKeyUtf8.Length()));
            UpdateHashLengthPrefixed(Hasher, Payload.GetData(), static_cast<uint32>(Payload.Num()));
        }
    }

    uint8 DigestBytes[FSHA1::DigestSize];
    Hasher.Final();
    Hasher.GetHash(DigestBytes);
    Result.Digest = FormatSha1HexLower(DigestBytes);
    return Result;
}

/**
 * @brief 获取错误码的诊断描述文本
 * @param InError 错误码
 * @return 中文诊断文本
 */
FString FStaticDataBinCodec::DescribeError(EStaticDataBinError InError) {
    switch (InError) {
        case EStaticDataBinError::None:
            return TEXT("无错误");
        case EStaticDataBinError::IoFailure:
            return TEXT("文件读取失败或数据结构截断");
        case EStaticDataBinError::MagicMismatch:
            return TEXT("Magic 魔数不匹配");
        case EStaticDataBinError::UnsupportedVersion:
            return TEXT("格式版本不受支持");
        case EStaticDataBinError::ChecksumMismatch:
            return TEXT("CRC32 校验和不匹配");
        case EStaticDataBinError::TableNotFound:
            return TEXT("未找到指定结构的数据表");
        case EStaticDataBinError::RowDeserializeFailed:
            return TEXT("行负载反序列化失败");
        case EStaticDataBinError::SerializerNotFound:
            return TEXT("未注册生成式负载读写器");
    }
    return TEXT("未知错误");
}

/**
 * @brief 将二进制编解码错误枚举映射为标准 Game::DataErrorCodes 错误码
 * @param InError 编解码错误枚举
 * @return 对应的数据模块整数错误码
 */
int32 FStaticDataBinCodec::ToDataErrorCode(EStaticDataBinError InError) noexcept {
    switch (InError) {
        case EStaticDataBinError::None:
            return Game::CommonErrorCodes::Success;
        case EStaticDataBinError::IoFailure:
            return Game::DataErrorCodes::BinIoFailure;
        case EStaticDataBinError::MagicMismatch:
            return Game::DataErrorCodes::BinMagicMismatch;
        case EStaticDataBinError::UnsupportedVersion:
            return Game::DataErrorCodes::BinUnsupportedVersion;
        case EStaticDataBinError::ChecksumMismatch:
            return Game::DataErrorCodes::BinChecksumMismatch;
        case EStaticDataBinError::TableNotFound:
            return Game::DataErrorCodes::BinTableNotFound;
        case EStaticDataBinError::RowDeserializeFailed:
            return Game::DataErrorCodes::BinRowDeserializeFailed;
        case EStaticDataBinError::SerializerNotFound:
            return Game::DataErrorCodes::BinSerializerNotFound;
    }
    return Game::DataErrorCodes::BinIoFailure;
}

/**
 * @brief 构造归属 Game::ModuleId::Data 的标准化 FGameError 对象
 * @param InError 编解码错误枚举
 * @return 包含模块 ID、数据错误码与诊断文本的 FGameError
 */
FGameError FStaticDataBinCodec::MakeError(EStaticDataBinError InError) {
    return FGameError::Make(Game::ModuleId::Data, ToDataErrorCode(InError), DescribeError(InError));
}

/**
 * @brief 解析负载区为表集合（临时装载，供解码成功时原子换入）
 * @param PayloadReader 已消费 Magic 与版本、定位到表数量字段的读取器
 * @param OutTables 输出表集合
 * @return 解析成功返回 true
 */
bool FStaticDataBinCodec::ParseTables(
    FMemoryReader& PayloadReader,
    TMap<FName, FStaticDataBinaryTable>& OutTables
) {
    int32 TableCount = 0;
    PayloadReader << TableCount;
    if (PayloadReader.IsError() || TableCount < 0 || static_cast<int64>(TableCount) * MinTableRecordBytes > RemainingBytes(PayloadReader)) {
        return false;
    }

    OutTables.Reserve(TableCount);
    for (int32 TableIndex = 0; TableIndex < TableCount; ++TableIndex) {
        FStaticDataBinaryTable Table;
        if (!ParseSingleTable(PayloadReader, Table)) {
            return false;
        }
        const FName TableName(*Table.StructName);
        if (TableName.IsNone() || OutTables.Contains(TableName)) {
            return false;
        }
        OutTables.Add(TableName, MoveTemp(Table));
    }
    return !PayloadReader.IsError();
}

/**
 * @brief 解析单张表的结构名、行数量与全部行负载
 * @param PayloadReader 定位到单表起始位置的读取器
 * @param OutTable 输出单表二进制镜像
 * @return 解析成功且无重复行键返回 true
 */
bool FStaticDataBinCodec::ParseSingleTable(FMemoryReader& PayloadReader, FStaticDataBinaryTable& OutTable) {
    PayloadReader << OutTable.StructName;
    if (PayloadReader.IsError() || OutTable.StructName.IsEmpty()) {
        return false;
    }

    int32 RowCount = 0;
    PayloadReader << RowCount;
    if (PayloadReader.IsError() || RowCount < 0 || static_cast<int64>(RowCount) * MinRowRecordBytes > RemainingBytes(PayloadReader)) {
        return false;
    }

    OutTable.Rows.Reserve(RowCount);
    for (int32 RowIndex = 0; RowIndex < RowCount; ++RowIndex) {
        FString RowKey;
        PayloadReader << RowKey;
        if (PayloadReader.IsError() || RowKey.IsEmpty()) {
            return false;
        }

        int32 PayloadLength = 0;
        PayloadReader << PayloadLength;
        const FName RowName(*RowKey);
        if (PayloadReader.IsError() || RowName.IsNone() || PayloadLength < 0 || PayloadLength > RemainingBytes(PayloadReader) || OutTable.Rows.Contains(RowName)) {
            return false;
        }

        TArray<uint8>& Payload = OutTable.Rows.Emplace(RowName);
        Payload.SetNumUninitialized(PayloadLength);
        if (PayloadLength > 0) {
            PayloadReader.Serialize(Payload.GetData(), PayloadLength);
            if (PayloadReader.IsError()) {
                return false;
            }
        }
    }
    return true;
}
