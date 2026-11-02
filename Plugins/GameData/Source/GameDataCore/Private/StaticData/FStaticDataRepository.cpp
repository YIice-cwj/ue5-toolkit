/**
 * @file FStaticDataRepository.cpp
 * @brief FStaticDataRepository 非模板实现（模板实现见 FStaticDataRepository.h）
 */

#include "StaticData/FStaticDataRepository.h"

#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "StaticData/FStaticDataBinCodec.h"

#include "Logging/GameLogCategories.h"

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
    TArray<uint8> FileBytes;
    if (!FStaticDataBinCodec::Encode(Tables, FileBytes)) {
        LastError = EStaticDataBinError::IoFailure;
        GAME_LOG(Error, TEXT("【静态数据仓库】导出二进制失败：编码器异常 (路径: %s)。"), *FilePath);
        return false;
    }

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

    const EStaticDataBinError DecodeError = FStaticDataBinCodec::Decode(FileBytes, Tables);
    if (DecodeError != EStaticDataBinError::None) {
        LastError = DecodeError;
        GAME_LOG(Warning, TEXT("【静态数据仓库】导入二进制失败：%s (路径: %s)。"),
            *FStaticDataBinCodec::DescribeError(DecodeError), *FilePath);
        return false;
    }

    LastError = EStaticDataBinError::None;
    GAME_LOG(Log, TEXT("【静态数据仓库】导入二进制成功: 成功装载 %d 张表 (路径: %s)。"),
        Tables.Num(), *FilePath);
    return true;
}

EStaticDataBinError FStaticDataRepository::GetLastError() const noexcept {
    return LastError;
}

FStaticDataContentDigest FStaticDataRepository::ComputeContentDigest() const {
    return FStaticDataBinCodec::ComputeDigest(Tables);
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
