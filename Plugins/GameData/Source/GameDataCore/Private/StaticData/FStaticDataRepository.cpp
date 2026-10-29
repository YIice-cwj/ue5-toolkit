/**
 * @file FStaticDataRepository.cpp
 * @brief FStaticDataRepository 非模板实现（模板实现见 FStaticDataRepository.h）
 */

#include "StaticData/FStaticDataRepository.h"

#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/NameTypes.h"

#include "Logging/GameLogCategories.h"

namespace {

/** 文件头 magic（0x54534442，对应字符 'T','S','D','B'） */
constexpr uint32 StaticDataBinMagic = (uint32('T') << 24) | (uint32('S') << 16) | (uint32('D') << 8) | uint32('B');

/** 当前支持的最高格式版本（v2 = 生成式免反射行负载；版本号用于拒绝旧档） */
constexpr uint32 StaticDataBinFormatVersion = 2;

int64 RemainingBytes(FMemoryReader& Reader) {
    return Reader.TotalSize() - Reader.Tell();
}

}  // namespace

void FStaticDataRepository::RegisterRowSerializer(const FName& StructName, FRowPayloadFunc PayloadFunc) {
    RowPayloadFuncs.Add(StructName, PayloadFunc);
    GAME_LOG(Verbose, TEXT("【静态数据仓库】注册行序列化器: 结构体=[%s]。"), *StructName.ToString());
}

int32 FStaticDataRepository::TableCount() const noexcept {
    return Tables.Num();
}

void FStaticDataRepository::Reset() noexcept {
    Tables.Empty();
    LastError = EStaticDataBinError::None;
}

bool FStaticDataRepository::ExportToFile(const FString& FilePath) const {
    TArray<uint8> PayloadBytes;
    FMemoryWriter PayloadWriter(PayloadBytes, false, true);
    uint32 FormatVersion = StaticDataBinFormatVersion;
    PayloadWriter << FormatVersion;
    int32 TotalTableCount = Tables.Num();
    PayloadWriter << TotalTableCount;
    for (const TPair<FName, FBinaryTable>& Pair : Tables) {
        const FBinaryTable& Table = Pair.Value;
        FString StructName = Table.StructName;
        PayloadWriter << StructName;
        int32 RowCount = Table.Rows.Num();
        PayloadWriter << RowCount;
        for (const TPair<FName, TArray<uint8>>& Row : Table.Rows) {
            FString RowKeyText = Row.Key.ToString();
            PayloadWriter << RowKeyText;
            int32 PayloadLength = Row.Value.Num();
            PayloadWriter << PayloadLength;
            PayloadWriter.Serialize(const_cast<uint8*>(Row.Value.GetData()), PayloadLength);
        }
    }
    if (PayloadWriter.IsError()) {
        LastError = EStaticDataBinError::IoFailure;
        GAME_LOG(Error, TEXT("【静态数据仓库】导出二进制失败：内存写入器异常 (路径: %s)。"), *FilePath);
        return false;
    }

    TArray<uint8> FileBytes;
    FileBytes.Reserve(sizeof(StaticDataBinMagic) + PayloadBytes.Num() + sizeof(uint32));
    FileBytes.Append(reinterpret_cast<const uint8*>(&StaticDataBinMagic), sizeof(StaticDataBinMagic));
    FileBytes.Append(PayloadBytes);
    const uint32 PayloadCrc = FCrc::MemCrc32(PayloadBytes.GetData(), PayloadBytes.Num());
    FileBytes.Append(reinterpret_cast<const uint8*>(&PayloadCrc), sizeof(PayloadCrc));

    if (!FFileHelper::SaveArrayToFile(FileBytes, *FilePath)) {
        LastError = EStaticDataBinError::IoFailure;
        GAME_LOG(Error, TEXT("【静态数据仓库】导出二进制失败：写盘 I/O 错误 (路径: %s)。"), *FilePath);
        return false;
    }
    LastError = EStaticDataBinError::None;
    GAME_LOG(Log, TEXT("【静态数据仓库】导出二进制成功: 表数量=%d, 字节大小=%d (路径: %s)。"),
        Tables.Num(), FileBytes.Num(), *FilePath);
    return true;
}

bool FStaticDataRepository::ImportFromFile(const FString& FilePath) {
    TArray<uint8> FileBytes;
    if (!FFileHelper::LoadFileToArray(FileBytes, *FilePath)) {
        LastError = EStaticDataBinError::IoFailure;
        GAME_LOG(Warning, TEXT("【静态数据仓库】导入二进制失败：文件不存在或无法读取 (路径: %s)。"), *FilePath);
        return false;
    }
    constexpr int32 MinFileSize = sizeof(StaticDataBinMagic) + sizeof(uint32) * 3;
    if (FileBytes.Num() < MinFileSize) {
        LastError = EStaticDataBinError::IoFailure;
        GAME_LOG(Warning, TEXT("【静态数据仓库】导入二进制失败：文件长度不足最小头要求 (%d < %d) (路径: %s)。"),
            FileBytes.Num(), MinFileSize, *FilePath);
        return false;
    }

    uint32 Magic = 0;
    FMemory::Memcpy(&Magic, FileBytes.GetData(), sizeof(Magic));
    if (Magic != StaticDataBinMagic) {
        LastError = EStaticDataBinError::MagicMismatch;
        GAME_LOG(Warning, TEXT("【静态数据仓库】导入二进制失败：Magic 校验不匹配 (读取: 0x%08X, 期望: 0x%08X)。"),
            Magic, StaticDataBinMagic);
        return false;
    }

    const uint8* PayloadStart = FileBytes.GetData() + sizeof(Magic);
    const int32 PayloadLength = FileBytes.Num() - sizeof(Magic) - sizeof(uint32);
    uint32 StoredCrc = 0;
    FMemory::Memcpy(&StoredCrc, PayloadStart + PayloadLength, sizeof(StoredCrc));
    if (FCrc::MemCrc32(PayloadStart, PayloadLength) != StoredCrc) {
        LastError = EStaticDataBinError::ChecksumMismatch;
        GAME_LOG(Warning, TEXT("【静态数据仓库】导入二进制失败：CRC 校验和不匹配 (文件 CRC 损坏)。"));
        return false;
    }

    FMemoryReader PayloadReader(FileBytes);
    PayloadReader.Seek(sizeof(uint32));
    uint32 FormatVersion = 0;
    PayloadReader << FormatVersion;
    if (PayloadReader.IsError()) {
        LastError = EStaticDataBinError::IoFailure;
        return false;
    }
    if (FormatVersion > StaticDataBinFormatVersion) {
        LastError = EStaticDataBinError::UnsupportedVersion;
        GAME_LOG(Warning, TEXT("【静态数据仓库】导入二进制失败：不支持的数据版本 %u (当前最高支持 %u)。"),
            FormatVersion, StaticDataBinFormatVersion);
        return false;
    }

    TMap<FName, FBinaryTable> IncomingTables;
    if (!ParseTables(PayloadReader, IncomingTables)) {
        LastError = EStaticDataBinError::IoFailure;
        return false;
    }
    Tables = MoveTemp(IncomingTables);
    LastError = EStaticDataBinError::None;
    GAME_LOG(Log, TEXT("【静态数据仓库】导入二进制成功: 成功装载 %d 张表 (路径: %s)。"),
        Tables.Num(), *FilePath);
    return true;
}

EStaticDataBinError FStaticDataRepository::GetLastError() const noexcept {
    return LastError;
}

FStaticDataContentDigest FStaticDataRepository::ComputeContentDigest() const {
    TArray<FName> SortedStructNames;
    Tables.GetKeys(SortedStructNames);
    SortedStructNames.Sort(FNameLexicalLess());

    FSHA1 Hasher;
    FStaticDataContentDigest Result;
    Result.TableCount = Tables.Num();
    for (const FName& StructName : SortedStructNames) {
        const FBinaryTable& Table = Tables.FindChecked(StructName);
        const FString StructNameText = StructName.ToString();
        const int32 RowCount = Table.Rows.Num();
        Hasher.Update(reinterpret_cast<const uint8*>(*StructNameText), static_cast<uint32>(StructNameText.Len() * sizeof(TCHAR)));
        Hasher.Update(reinterpret_cast<const uint8*>(&RowCount), sizeof(RowCount));

        TArray<FName> SortedRowKeys;
        Table.Rows.GetKeys(SortedRowKeys);
        SortedRowKeys.Sort(FNameLexicalLess());
        Result.RowCount += SortedRowKeys.Num();
        for (const FName& RowKey : SortedRowKeys) {
            const TArray<uint8>& Payload = Table.Rows.FindChecked(RowKey);
            const FString RowKeyText = RowKey.ToString();
            Hasher.Update(reinterpret_cast<const uint8*>(*RowKeyText), static_cast<uint32>(RowKeyText.Len() * sizeof(TCHAR)));
            if (Payload.Num() > 0) {
                Hasher.Update(Payload.GetData(), static_cast<uint32>(Payload.Num()));
            }
        }
    }

    uint8 DigestBytes[FSHA1::DigestSize];
    Hasher.Final();
    Hasher.GetHash(DigestBytes);
    Result.Digest.Reserve(FSHA1::DigestSize * 2);
    for (const uint8 Byte : DigestBytes) {
        Result.Digest += FString::Printf(TEXT("%02x"), Byte);
    }
    return Result;
}

bool FStaticDataRepository::SerializeRowToPayload(const FName& StructName, void* RowPtr, TArray<uint8>& OutPayload) const {
    const FRowPayloadFunc PayloadFunc = RowPayloadFuncs.FindRef(StructName);
    if (PayloadFunc == nullptr) {
        LastError = EStaticDataBinError::SerializerNotFound;
        return false;
    }

    FMemoryWriter PayloadWriter(OutPayload, false, true);
    if (!PayloadFunc(PayloadWriter, RowPtr)) {
        LastError = EStaticDataBinError::IoFailure;
        return false;
    }
    return true;
}

bool FStaticDataRepository::DeserializeRowFromPayload(const FName& StructName, const TArray<uint8>& InPayload, void* OutRowPtr) const {
    const FRowPayloadFunc PayloadFunc = RowPayloadFuncs.FindRef(StructName);
    if (PayloadFunc == nullptr) {
        LastError = EStaticDataBinError::SerializerNotFound;
        return false;
    }

    FMemoryReader PayloadReader(InPayload);
    if (!PayloadFunc(PayloadReader, OutRowPtr)) {
        LastError = EStaticDataBinError::RowDeserializeFailed;
        return false;
    }
    return true;
}

bool FStaticDataRepository::ParseTables(FMemoryReader& PayloadReader, TMap<FName, FBinaryTable>& OutTables) {
    int32 TableCount = 0;
    PayloadReader << TableCount;
    if (PayloadReader.IsError() || TableCount < 0 || TableCount > RemainingBytes(PayloadReader)) {
        return false;
    }
    for (int32 TableIndex = 0; TableIndex < TableCount; ++TableIndex) {
        FBinaryTable Table;
        PayloadReader << Table.StructName;

        int32 RowCount = 0;
        PayloadReader << RowCount;
        if (PayloadReader.IsError() || RowCount < 0 || RowCount > RemainingBytes(PayloadReader)) {
            return false;
        }
        Table.Rows.Reserve(RowCount);
        for (int32 RowIndex = 0; RowIndex < RowCount; ++RowIndex) {
            FString RowKey;
            PayloadReader << RowKey;

            int32 PayloadLength = 0;
            PayloadReader << PayloadLength;
            const FName RowName(*RowKey);
            if (PayloadReader.IsError() || PayloadLength < 0 || PayloadLength > RemainingBytes(PayloadReader) || Table.Rows.Contains(RowName)) {
                return false;
            }
            TArray<uint8>& Payload = Table.Rows.Emplace(RowName);
            Payload.SetNumUninitialized(PayloadLength);
            PayloadReader.Serialize(Payload.GetData(), PayloadLength);
            if (PayloadReader.IsError()) {
                return false;
            }
        }
        const FName TableName(*Table.StructName);
        if (OutTables.Contains(TableName)) {
            return false;
        }
        OutTables.Add(TableName, MoveTemp(Table));
    }
    return !PayloadReader.IsError();
}
