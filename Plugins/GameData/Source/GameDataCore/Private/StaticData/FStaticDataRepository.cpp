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

FStaticDataRepository::FStaticDataRepository(const FStaticDataRepository& Other)
    : Tables(Other.Tables)
    , RowPayloadFuncs(Other.RowPayloadFuncs)
    , LastError(Other.LastError.load(std::memory_order_relaxed)) {
}

FStaticDataRepository& FStaticDataRepository::operator=(const FStaticDataRepository& Other) {
    if (this != &Other) {
        Tables = Other.Tables;
        RowPayloadFuncs = Other.RowPayloadFuncs;
        LastError.store(Other.LastError.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return *this;
}

FStaticDataRepository::FStaticDataRepository(FStaticDataRepository&& Other) noexcept
    : Tables(MoveTemp(Other.Tables))
    , RowPayloadFuncs(MoveTemp(Other.RowPayloadFuncs))
    , LastError(Other.LastError.load(std::memory_order_relaxed)) {
}

FStaticDataRepository& FStaticDataRepository::operator=(FStaticDataRepository&& Other) noexcept {
    if (this != &Other) {
        Tables = MoveTemp(Other.Tables);
        RowPayloadFuncs = MoveTemp(Other.RowPayloadFuncs);
        LastError.store(Other.LastError.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return *this;
}

/**
 * @brief 登记某行结构的生成式负载读写器（同结构重复登记即覆盖）
 * @param StructName 行结构名（与 RowType::StaticStruct()->GetName() 严格一致）
 * @param PayloadFunc 行负载读写函数（由数据表生成管线产出）
 */
void FStaticDataRepository::RegisterRowSerializer(const FName& StructName, FRowPayloadFunc PayloadFunc) {
    RowPayloadFuncs.Add(StructName, PayloadFunc);
    GAME_LOG(Verbose, TEXT("【静态数据仓库】注册行序列化器: 结构体=[%s]。"), *StructName.ToString());
}

/**
 * @brief 查询当前装载表数
 * @return 表数量
 */
int32 FStaticDataRepository::TableCount() const noexcept {
    return Tables.Num();
}

/**
 * @brief 清空全部装载数据并复位错误码（已登记的负载读写器保留）
 */
void FStaticDataRepository::Reset() noexcept {
    Tables.Empty();
    LastError.store(EStaticDataBinError::None, std::memory_order_relaxed);
}

/**
 * @brief 导出全部装载表至 .bin 文件（脱机导出，Cook 前置；字节编码委托 FStaticDataBinCodec）
 * @param FilePath 目标文件绝对路径
 * @return 成功返回 Success；编码或写盘失败返回 Failure
 */
TGameStatusResult<void> FStaticDataRepository::ExportToFile(const FString& FilePath) const {
    auto EncodeResult = FStaticDataBinCodec::Encode(Tables);
    if (EncodeResult.IsFailure()) {
        const EStaticDataBinError ErrorCode = EncodeResult.GetError();
        LastError.store(ErrorCode, std::memory_order_relaxed);
        GAME_LOG(Error, TEXT("【静态数据仓库】导出二进制失败：编码器异常 (路径: %s)。"), *FilePath);
        return TGameStatusResult<void>::Failure(static_cast<int32>(ErrorCode), FStaticDataBinCodec::DescribeError(ErrorCode));
    }

    const TArray<uint8>& FileBytes = EncodeResult.GetValue();
    if (!FFileHelper::SaveArrayToFile(FileBytes, *FilePath)) {
        LastError.store(EStaticDataBinError::IoFailure, std::memory_order_relaxed);
        GAME_LOG(Error, TEXT("【静态数据仓库】导出二进制失败：写盘 I/O 错误 (路径: %s)。"), *FilePath);
        return TGameStatusResult<void>::Failure(static_cast<int32>(EStaticDataBinError::IoFailure), FStaticDataBinCodec::DescribeError(EStaticDataBinError::IoFailure));
    }
    LastError.store(EStaticDataBinError::None, std::memory_order_relaxed);
    GAME_LOG(Log, TEXT("【静态数据仓库】导出二进制成功: 表数量=%d, 字节大小=%d (路径: %s)。"),
        Tables.Num(), FileBytes.Num(), *FilePath);
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 自 .bin 文件装载全部表（校验 Magic / 版本 / CRC，失败保持原状）
 * @param FilePath 源文件绝对路径
 * @return 成功返回包含装载表数的 Result，失败返回错误枚举
 */
TGameResult<int32, EStaticDataBinError> FStaticDataRepository::ImportFromFile(const FString& FilePath) {
    TArray<uint8> FileBytes;
    if (!FFileHelper::LoadFileToArray(FileBytes, *FilePath)) {
        LastError.store(EStaticDataBinError::IoFailure, std::memory_order_relaxed);
        GAME_LOG(Warning, TEXT("【静态数据仓库】导入二进制失败：文件不存在或无法读取 (路径: %s)。"), *FilePath);
        return TGameResult<int32, EStaticDataBinError>::Failure(EStaticDataBinError::IoFailure);
    }

    auto DecodeResult = FStaticDataBinCodec::Decode(FileBytes);
    if (DecodeResult.IsFailure()) {
        const EStaticDataBinError ErrorCode = DecodeResult.GetError();
        LastError.store(ErrorCode, std::memory_order_relaxed);
        GAME_LOG(Warning, TEXT("【静态数据仓库】导入二进制失败：%s (路径: %s)。"),
            *FStaticDataBinCodec::DescribeError(ErrorCode), *FilePath);
        return TGameResult<int32, EStaticDataBinError>::Failure(ErrorCode);
    }

    Tables = DecodeResult.ExtractValue();
    LastError.store(EStaticDataBinError::None, std::memory_order_relaxed);
    GAME_LOG(Log, TEXT("【静态数据仓库】导入二进制成功: 成功装载 %d 张表 (路径: %s)。"),
        Tables.Num(), *FilePath);
    return TGameResult<int32, EStaticDataBinError>::Success(Tables.Num());
}

/**
 * @brief 获取最近一次失败的错误码
 * @return 错误码（无错误为 None）
 */
EStaticDataBinError FStaticDataRepository::GetLastError() const noexcept {
    return LastError.load(std::memory_order_relaxed);
}

/**
 * @brief 计算仓库内容摘要（与装载顺序无关，供双通道数据一致性对拍）
 * @return 表数、总行数与 SHA-1 摘要；空仓库返回 SHA-1 空输入摘要
 */
FStaticDataContentDigest FStaticDataRepository::ComputeContentDigest() const {
    return FStaticDataBinCodec::ComputeDigest(Tables);
}

/**
 * @brief 行结构体二进制序列化（经登记读写器，无类型知识）
 * @param StructName 行结构名（须已登记读写器）
 * @param RowPtr 行数据指针（读写器只读，不修改行数据）
 * @param OutPayload 输出二进制负载
 * @param OutError 输出错误枚举
 * @return 序列化成功返回 true；未登记读写器或归档写出失败返回 false
 */
bool FStaticDataRepository::SerializeRowToPayload(const FName& StructName, void* RowPtr, TArray<uint8>& OutPayload, EStaticDataBinError& OutError) const {
    const FRowPayloadFunc PayloadFunc = RowPayloadFuncs.FindRef(StructName);
    if (PayloadFunc == nullptr) {
        OutError = EStaticDataBinError::SerializerNotFound;
        return false;
    }

    FMemoryWriter PayloadWriter(OutPayload, false, true);
    if (!PayloadFunc(PayloadWriter, RowPtr)) {
        OutError = EStaticDataBinError::IoFailure;
        return false;
    }
    OutError = EStaticDataBinError::None;
    return true;
}

/**
 * @brief 行二进制负载反序列化（越界/损坏经 FMemoryReader::IsError 检出）
 * @param StructName 行结构名（须已登记读写器）
 * @param InPayload 输入二进制负载
 * @param OutRowPtr 输出行数据缓冲
 * @param OutError 输出错误枚举
 * @return 反序列化成功返回 true；未登记读写器或负载损坏返回 false
 */
bool FStaticDataRepository::DeserializeRowFromPayload(const FName& StructName, const TArray<uint8>& InPayload, void* OutRowPtr, EStaticDataBinError& OutError) const {
    const FRowPayloadFunc PayloadFunc = RowPayloadFuncs.FindRef(StructName);
    if (PayloadFunc == nullptr) {
        OutError = EStaticDataBinError::SerializerNotFound;
        return false;
    }

    FMemoryReader PayloadReader(InPayload);
    if (!PayloadFunc(PayloadReader, OutRowPtr)) {
        OutError = EStaticDataBinError::RowDeserializeFailed;
        return false;
    }
    OutError = EStaticDataBinError::None;
    return true;
}
