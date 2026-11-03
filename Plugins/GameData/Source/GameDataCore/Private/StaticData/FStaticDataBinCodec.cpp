/**
 * @file FStaticDataBinCodec.cpp
 * @brief .bin 文件格式编解码器实现
 */

#include "StaticData/FStaticDataBinCodec.h"

#include "Misc/Crc.h"
#include "Misc/SecureHash.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/NameTypes.h"

namespace {

constexpr uint32 StaticDataBinMagic = (uint32('T') << 24) | (uint32('S') << 16) | (uint32('D') << 8) | uint32('B');
constexpr uint32 StaticDataBinFormatVersion = 2;

int64 RemainingBytes(FMemoryReader& Reader) {
    return Reader.TotalSize() - Reader.Tell();
}

}

bool FStaticDataBinCodec::Encode(const TMap<FName, FStaticDataBinaryTable>& InTables, TArray<uint8>& OutFileBytes) {
    TArray<uint8> PayloadBytes;
    FMemoryWriter PayloadWriter(PayloadBytes, false, true);
    uint32 FormatVersion = StaticDataBinFormatVersion;
    PayloadWriter << FormatVersion;
    int32 TotalTableCount = InTables.Num();
    PayloadWriter << TotalTableCount;
    for (const TPair<FName, FStaticDataBinaryTable>& Pair : InTables) {
        const FStaticDataBinaryTable& Table = Pair.Value;
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
        return false;
    }

    OutFileBytes.Reset();
    OutFileBytes.Reserve(sizeof(StaticDataBinMagic) + PayloadBytes.Num() + sizeof(uint32));
    OutFileBytes.Append(reinterpret_cast<const uint8*>(&StaticDataBinMagic), sizeof(StaticDataBinMagic));
    OutFileBytes.Append(PayloadBytes);
    const uint32 PayloadCrc = FCrc::MemCrc32(PayloadBytes.GetData(), PayloadBytes.Num());
    OutFileBytes.Append(reinterpret_cast<const uint8*>(&PayloadCrc), sizeof(PayloadCrc));
    return true;
}

EStaticDataBinError FStaticDataBinCodec::Decode(
    const TArray<uint8>& InFileBytes,
    TMap<FName, FStaticDataBinaryTable>& OutTables
) {
    constexpr int32 MinFileSize = sizeof(StaticDataBinMagic) + sizeof(uint32) * 3;
    if (InFileBytes.Num() < MinFileSize) {
        return EStaticDataBinError::IoFailure;
    }

    uint32 Magic = 0;
    FMemory::Memcpy(&Magic, InFileBytes.GetData(), sizeof(Magic));
    if (Magic != StaticDataBinMagic) {
        return EStaticDataBinError::MagicMismatch;
    }

    const uint8* PayloadStart = InFileBytes.GetData() + sizeof(Magic);
    const int32 PayloadLength = InFileBytes.Num() - sizeof(Magic) - sizeof(uint32);
    uint32 StoredCrc = 0;
    FMemory::Memcpy(&StoredCrc, PayloadStart + PayloadLength, sizeof(StoredCrc));
    if (FCrc::MemCrc32(PayloadStart, PayloadLength) != StoredCrc) {
        return EStaticDataBinError::ChecksumMismatch;
    }

    FMemoryReader PayloadReader(InFileBytes);
    PayloadReader.Seek(sizeof(uint32));
    uint32 FormatVersion = 0;
    PayloadReader << FormatVersion;
    if (PayloadReader.IsError()) {
        return EStaticDataBinError::IoFailure;
    }
    if (FormatVersion > StaticDataBinFormatVersion) {
        return EStaticDataBinError::UnsupportedVersion;
    }

    TMap<FName, FStaticDataBinaryTable> IncomingTables;
    if (!ParseTables(PayloadReader, IncomingTables)) {
        return EStaticDataBinError::IoFailure;
    }
    OutTables = MoveTemp(IncomingTables);
    return EStaticDataBinError::None;
}

FStaticDataContentDigest FStaticDataBinCodec::ComputeDigest(const TMap<FName, FStaticDataBinaryTable>& InTables) {
    TArray<FName> SortedStructNames;
    InTables.GetKeys(SortedStructNames);
    SortedStructNames.Sort(FNameLexicalLess());

    FSHA1 Hasher;
    FStaticDataContentDigest Result;
    Result.TableCount = InTables.Num();
    for (const FName& StructName : SortedStructNames) {
        const FStaticDataBinaryTable& Table = InTables.FindChecked(StructName);
        const FString StructNameText = StructName.ToString();
        const FTCHARToUTF8 StructNameUtf8(*StructNameText);
        const int32 RowCount = Table.Rows.Num();
        Hasher.Update(reinterpret_cast<const uint8*>(StructNameUtf8.Get()), static_cast<uint32>(StructNameUtf8.Length()));
        Hasher.Update(reinterpret_cast<const uint8*>(&RowCount), sizeof(RowCount));

        TArray<FName> SortedRowKeys;
        Table.Rows.GetKeys(SortedRowKeys);
        SortedRowKeys.Sort(FNameLexicalLess());
        Result.RowCount += SortedRowKeys.Num();
        for (const FName& RowKey : SortedRowKeys) {
            const TArray<uint8>& Payload = Table.Rows.FindChecked(RowKey);
            const FString RowKeyText = RowKey.ToString();
            const FTCHARToUTF8 RowKeyUtf8(*RowKeyText);
            Hasher.Update(reinterpret_cast<const uint8*>(RowKeyUtf8.Get()), static_cast<uint32>(RowKeyUtf8.Length()));
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

FString FStaticDataBinCodec::DescribeError(EStaticDataBinError InError) {
    switch (InError) {
        case EStaticDataBinError::None:
            return TEXT("无错误");
        case EStaticDataBinError::IoFailure:
            return TEXT("文件读取失败或数据结构截断");
        case EStaticDataBinError::MagicMismatch:
            return TEXT("Magic 魔数不匹配");
        case EStaticDataBinError::UnsupportedVersion:
            return TEXT("格式版本高于当前支持版本");
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

bool FStaticDataBinCodec::ParseTables(
    FMemoryReader& PayloadReader,
    TMap<FName, FStaticDataBinaryTable>& OutTables
) {
    int32 TableCount = 0;
    PayloadReader << TableCount;
    if (PayloadReader.IsError() || TableCount < 0 || TableCount > RemainingBytes(PayloadReader)) {
        return false;
    }
    for (int32 TableIndex = 0; TableIndex < TableCount; ++TableIndex) {
        FStaticDataBinaryTable Table;
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
